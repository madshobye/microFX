export class DeviceProtocol {
  constructor(sendRaw, options = {}) {
    this.sendRaw = sendRaw;
    this.timeoutMs = options.timeoutMs ?? 15000;
    this.createId = options.createId ?? (() => crypto.randomUUID?.() || `${Date.now()}-${Math.random()}`);
    this.onEvent = options.onEvent ?? (() => {});
    this.pending = new Map();
  }

  request(type, fields = {}) {
    const id = this.createId();
    const payload = { id, type, ...fields };
    return new Promise((resolve, reject) => {
      const started = Date.now();
      const timer = setTimeout(() => {
        this.pending.delete(id);
        this.onEvent({ phase: "timeout", id, type, elapsedMs: Date.now() - started });
        reject(new Error(`${type} timed out`));
      }, this.timeoutMs);
      this.pending.set(id, { resolve, reject, timer, type, started });
      try {
        this.sendRaw(JSON.stringify(payload));
        this.onEvent({ phase: "sent", id, type, elapsedMs: 0 });
      } catch (error) {
        clearTimeout(timer);
        this.pending.delete(id);
        this.onEvent({ phase: "send-error", id, type, elapsedMs: Date.now() - started,
          detail: error?.message || String(error) });
        reject(error);
      }
    });
  }

  receive(response) {
    const pending = this.pending.get(response?.id);
    if (!pending) {
      this.onEvent({ phase: "unmatched", id: response?.id || "", type: response?.type || "" });
      return false;
    }
    clearTimeout(pending.timer);
    this.pending.delete(response.id);
    this.onEvent({ phase: "received", id: response.id, type: pending.type,
      responseType: response?.type || "", ok: Boolean(response?.ok),
      elapsedMs: Date.now() - pending.started,
      detail: response?.ok ? "" : (response?.error || "Device error") });
    if (response.ok) pending.resolve(response);
    else pending.reject(new Error(response.error || "Device error"));
    return true;
  }

  close(reason = "Device disconnected") {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      this.onEvent({ phase: "closed", id: "", type: pending.type,
        elapsedMs: Date.now() - pending.started, detail: reason });
      pending.reject(new Error(reason));
    }
    this.pending.clear();
  }
}

const ASSET_CHUNK_BYTES = 48 * 1024;

export function operationToken(prefix = "operation") {
  return `${prefix}-${globalThis.crypto?.randomUUID?.() || `${Date.now()}-${Math.random()}`}`;
}

function bytesToBase64(bytes) {
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  return btoa(binary);
}

function base64ToBytes(value) {
  const binary = atob(value);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index++) bytes[index] = binary.charCodeAt(index);
  return bytes;
}

function uploadToken(project, path, bytes) {
  let hash = 2166136261;
  const mix = (value) => { hash ^= value; hash = Math.imul(hash, 16777619) >>> 0; };
  for (const character of `${project}\0${path}\0${bytes.length}\0`) mix(character.charCodeAt(0));
  for (const value of bytes) mix(value);
  return `asset-${hash.toString(16).padStart(8, "0")}-${bytes.length}`;
}

export async function uploadAsset(protocol, project, path, value, options = {}) {
  const bytes = value instanceof Uint8Array ? value : new Uint8Array(value);
  const upload = options.upload ?? uploadToken(project, path, bytes);
  const chunkSize = options.chunkSize ?? ASSET_CHUNK_BYTES;
  if (!Number.isSafeInteger(chunkSize) || chunkSize < 1 || chunkSize > 128 * 1024)
    throw new Error("Invalid asset chunk size");
  let status = await protocol.request("asset.upload.status", {
    project, path, upload, size: bytes.length
  });
  let offset = Number(status.offset);
  if (!Number.isSafeInteger(offset) || offset < 0 || offset > bytes.length)
    throw new Error("Device returned an invalid upload offset");
  while (offset < bytes.length) {
    const content = bytes.subarray(offset, Math.min(offset + chunkSize, bytes.length));
    status = await protocol.request("asset.upload.chunk", {
      project, path, upload, size: bytes.length, offset, content: bytesToBase64(content)
    });
    const next = Number(status.offset);
    if (!Number.isSafeInteger(next) || next <= offset || next > bytes.length)
      throw new Error("Device did not advance the asset upload");
    offset = next;
    options.onProgress?.({ offset, size: bytes.length });
  }
  return protocol.request("asset.upload.commit", {
    project, path, upload, size: bytes.length
  });
}

export async function downloadAsset(protocol, project, path, options = {}) {
  const chunks = [];
  let offset = 0;
  let size = null;
  do {
    const response = await protocol.request("asset.get.chunk", { project, path, offset });
    const bytes = base64ToBytes(response.content || "");
    if (Number(response.offset) !== offset || Number(response.next) !== offset + bytes.length)
      throw new Error("Device returned a discontinuous asset chunk");
    if (size === null) size = Number(response.size);
    if (Number(response.size) !== size || !Number.isSafeInteger(size) || size < 0)
      throw new Error("Asset changed while downloading");
    chunks.push(bytes);
    offset += bytes.length;
    options.onProgress?.({ offset, size });
    if (response.eof) break;
    if (!bytes.length) throw new Error("Device returned an empty asset chunk");
  } while (true);
  if (offset !== size) throw new Error("Asset size mismatch");
  const result = new Uint8Array(size);
  let cursor = 0;
  for (const chunk of chunks) { result.set(chunk, cursor); cursor += chunk.length; }
  return result;
}

export async function saveProject(protocol, project, code, run = false, options = {}) {
  if (!project) throw new Error("No project selected");
  const onState = options.onState ?? (() => {});
  onState({ state: "saving", project });
  if (!run) {
    await protocol.request("code.put", { project, content: code });
    const saved = { state: "saved", project };
    onState(saved);
    return saved;
  }
  const activation = options.activation ?? operationToken("activation");
  const requested = await protocol.request("project.save-run", {
    project, content: code, activation
  });
  if (requested.activation !== activation)
    throw new Error("Device did not acknowledge the activation token");
  onState({ state: "activating", project, activation });
  return waitForActivation(protocol, activation, { ...options, project });
}

export async function waitForActivation(protocol, activation, options = {}) {
  const timeoutMs = options.timeoutMs ?? 20000;
  const pollMs = options.pollMs ?? 250;
  const delay = options.delay ?? ((duration) => new Promise((resolve) => setTimeout(resolve, duration)));
  const deadline = Date.now() + timeoutMs;
  const onState = options.onState ?? (() => {});
  let previousState = "";
  while (Date.now() <= deadline) {
    const status = await protocol.request("project.status", { activation });
    if (status.state !== previousState) {
      previousState = status.state;
      onState({ ...status, activation, project: status.project || options.project || "" });
    }
    if (status.state === "running") return status;
    if (status.state === "failed") {
      throw new Error(status.detail || `Project ${status.project || ""} failed to start`.trim());
    }
    await delay(pollMs);
  }
  onState({ state: "failed", activation, project: options.project || "", detail: "Project activation timed out" });
  throw new Error("Project activation timed out");
}
