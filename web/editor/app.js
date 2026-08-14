import { DeviceProtocol, downloadAsset, operationToken, uploadAsset } from "./protocol.js";
import { createStudioActions } from "./actions.js";
import { recentPeerIds, rememberPeerId, uploadAssetBatch } from "./studio-state.js";
import { ReconnectSession } from "./reconnect-session.js";
import { runInteractionCheck, verifyDeviceProtocol } from "./interaction-check.js";

const $ = (selector) => document.querySelector(selector);
const product = Object.freeze({ name: "microFX", slug: "microfx" });
document.title = `${product.name} Studio`;
$("#product-title").textContent = document.title;
const editor = ace.edit("editor");
editor.setTheme("ace/theme/tomorrow_night_eighties");
editor.session.setMode("ace/mode/javascript");
editor.session.setUseSoftTabs(true);
editor.session.setTabSize(2);
editor.setOptions({ showPrintMargin: false, wrap: true });

const state = { peer: null, connection: null, protocol: null, connectTimer: null,
  reconnectTimer: null, consoleTimer: null, consoleCursor: 0, intentionalClose: false,
  assets: [], projects: [], project: "", metadata: {}, revisions: [], inspectedRevision: "", negotiation: 0,
  protocolReady: false };
const reconnectSession = new ReconnectSession({ timeoutMs: 90000 });
const controls = ["#retrieve", "#save", "#run", "#asset-input", "#project", "#new-project", "#disconnect",
  "#project-title", "#project-description", "#save-metadata", "#system-check"];
const peerIdInput = $("#peer-id");
const peerHistoryKey = `${product.slug}.peerIds`;
const queryPeerId = new URLSearchParams(globalThis.location?.search || "").get("peer") || "";
const requestedPeerId = /^[A-Za-z0-9._-]{1,64}$/.test(queryPeerId) ? queryPeerId : "";
const legacyPeerId = requestedPeerId || localStorage.getItem(`${product.slug}.peerId`) || `${product.slug}-demo`;

function renderPeerHistory() {
  const peers = recentPeerIds(localStorage.getItem(peerHistoryKey), legacyPeerId);
  $("#peer-history").replaceChildren(...peers.map((peerId) => {
    const option = document.createElement("option"); option.value = peerId; return option;
  }));
  if (!peerIdInput.value) peerIdInput.value = peers[0] || `${product.slug}-demo`;
}
renderPeerHistory();

function setStatus(text, kind = "idle") {
  $("#status").textContent = text;
  $("#status").dataset.state = kind;
}

function message(text) { $("#message").textContent = text; $("#console").textContent = `${new Date().toLocaleTimeString()} · ${text}`; }
function enabled(value) { controls.forEach((id) => { $(id).disabled = !value; }); }
function errorText(error, fallback = "PeerJS connection failed") {
  return error?.message || error?.type || String(error || fallback) || fallback;
}

function appendTrace(text) {
  const output = $("#transport-console");
  if (output.textContent === "No device interaction yet") output.textContent = "";
  output.textContent += `${new Date().toLocaleTimeString()}  ${text}\n`;
  if (output.textContent.length > 30000) output.textContent = output.textContent.slice(-30000);
  output.scrollTop = output.scrollHeight;
}

function traceProtocol(event) {
  const identity = event.id ? ` ${event.id}` : "";
  const duration = Number.isFinite(event.elapsedMs) ? ` ${Math.round(event.elapsedMs)}ms` : "";
  const outcome = event.phase === "received"
    ? ` ${event.ok ? "ok" : "error"}${event.responseType ? ` → ${event.responseType}` : ""}`
    : event.detail ? ` ${event.detail}` : "";
  const line = `${event.phase.toUpperCase()} ${event.type || "response"}${identity}${duration}${outcome}`;
  appendTrace(line);
  console.info(`PROTOCOL ${state.negotiation} BROWSER`, line);
}

function shouldDropCandidate(candidate) {
  if (!candidate) return false;
  const protocol = candidate.match(/\s(udp|tcp)\s/i)?.[1]?.toLowerCase() || "";
  return protocol === "tcp";
}

function sdpShape(sdp) {
  const sections = String(sdp || "").split(/(?=^m=)/m).filter((part) => /^m=/m.test(part));
  return sections.map((section) => {
    const media = section.match(/^m=([^\r\n]+)/m)?.[1] || "?";
    const mid = section.match(/^a=mid:([^\r\n]+)/m)?.[1] || "<none>";
    return `m=${media} mid=${mid}`;
  }).join(", ");
}

function candidateShape(candidate) {
  const fields = String(candidate || "").trim().split(/\s+/);
  const typeAt = fields.indexOf("typ");
  return fields.length >= 8
    ? `protocol=${fields[2]} endpoint=${fields[4]}:${fields[5]} type=${typeAt >= 0 ? fields[typeAt + 1] : "?"}`
    : "malformed";
}

function installCandidateFilter(peer) {
  const socket = peer?.socket;
  if (!socket || typeof socket.send !== "function" || socket._microfxCandidateFilter) return;
  const sendSignal = socket.send.bind(socket);
  socket._microfxCandidateFilter = true;
  socket.send = (signal) => {
    const candidate = signal?.payload?.candidate?.candidate || "";
    if (signal?.type === "OFFER") console.info(`NEGOTIATION ${state.negotiation} BROWSER offer-shape`, sdpShape(signal?.payload?.sdp?.sdp));
    if (signal?.type === "CANDIDATE") {
      const dropped = shouldDropCandidate(candidate);
      console.info(`NEGOTIATION ${state.negotiation} BROWSER candidate-out`, candidateShape(candidate), dropped ? "dropped=tcp" : "");
      if (dropped) return;
    }
    return sendSignal(signal);
  };
}

function stripTcpCandidates(sdp) {
  return String(sdp || "")
    .split(/\r?\n/)
    .filter((line) => !/^a=candidate:/i.test(line) || !/\stcp\s/i.test(line))
    .join("\r\n");
}

function installAnswerFilter(connection) {
  if (!connection || typeof connection.handleMessage !== "function" || connection._microfxAnswerFilter) return;
  const handleMessage = connection.handleMessage.bind(connection);
  connection._microfxAnswerFilter = true;
  connection.handleMessage = (signal) => {
    if (signal?.type === "ANSWER" && signal.payload?.sdp?.sdp) {
      signal = {
        ...signal,
        payload: {
          ...signal.payload,
          sdp: { ...signal.payload.sdp, sdp: stripTcpCandidates(signal.payload.sdp.sdp) }
        }
      };
      console.info(`NEGOTIATION ${state.negotiation} BROWSER answer-shape`, sdpShape(signal.payload.sdp.sdp));
    } else if (signal?.type === "CANDIDATE") {
      console.info(`NEGOTIATION ${state.negotiation} BROWSER candidate-in`, candidateShape(signal?.payload?.candidate?.candidate));
    }
    return handleMessage(signal);
  };
}

async function decodeMessage(value) {
  if (typeof value === "string") return value;
  if (value instanceof ArrayBuffer) return new TextDecoder().decode(value);
  if (ArrayBuffer.isView(value)) return new TextDecoder().decode(value);
  if (value instanceof Blob) return value.text();
  return String(value);
}

function sendCurrent(type, fields = {}) {
  if (!state.connection?.open) return Promise.reject(new Error("Device is not connected"));
  return state.protocol.request(type, fields);
}

function send(type, fields = {}, options = {}) {
  return reconnectSession.request(type, fields, options);
}

function stopConsole() { clearTimeout(state.consoleTimer); state.consoleTimer = null; }

function closeCurrent(intentional = false, preserveOperations = false) {
  state.intentionalClose = intentional;
  clearTimeout(state.connectTimer);
  clearTimeout(state.reconnectTimer);
  stopConsole();
  state.connectTimer = null;
  const protocol = state.protocol;
  if (preserveOperations) reconnectSession.detach(protocol);
  else if (intentional) reconnectSession.cancel("Disconnected by user");
  state.connection?.close();
  protocol?.close(intentional ? "Device disconnected" : "Connection lost");
  state.peer?.destroy();
  state.connection = null;
  state.protocol = null;
  state.peer = null;
  state.protocolReady = false;
  state.inspectedRevision = "";
  if ($("#revision-dialog").open) $("#revision-dialog").close();
  enabled(false);
  setStatus("offline");
}

function scheduleReconnect() {
  if (state.intentionalClose || state.reconnectTimer) return;
  setStatus("retrying");
  message("Connection lost; retrying in 3 seconds");
  state.reconnectTimer = setTimeout(() => { state.reconnectTimer = null; connect(true); }, 3000);
}

function connectionFailed(text) {
  if (state.intentionalClose) return;
  clearTimeout(state.connectTimer);
  state.connectTimer = null;
  setStatus("error", "error");
  message(text);
  const protocol = state.protocol;
  reconnectSession.detach(protocol);
  state.connection?.close();
  protocol?.close(text);
  state.peer?.destroy();
  state.connection = null; state.protocol = null; state.peer = null;
  enabled(false); stopConsole(); scheduleReconnect();
}

async function pollConsole() {
  if (!state.connection?.open) return;
  try {
    const response = await sendCurrent("console.get", { cursor: state.consoleCursor });
    state.consoleCursor = Number(response.cursor || 0);
    if (response.content) {
      const output = $("#device-console");
      output.textContent += response.content;
      if (output.textContent.length > 50000) output.textContent = output.textContent.slice(-50000);
      output.scrollTop = output.scrollHeight;
    }
  } catch (error) {
    if (state.connection?.open) message(`Console: ${error.message}`);
  }
  if (state.connection?.open) state.consoleTimer = setTimeout(pollConsole, 2000);
}

async function connect(retrying = false) {
  closeCurrent(true, retrying);
  state.intentionalClose = false;
  state.negotiation += 1;
  console.info(`NEGOTIATION ${state.negotiation} BROWSER begin`);
  const remoteId = peerIdInput.value.trim();
  if (!remoteId) return;
  localStorage.setItem(`${product.slug}.peerId`, remoteId);
  localStorage.setItem(peerHistoryKey, JSON.stringify(rememberPeerId(localStorage.getItem(peerHistoryKey), remoteId)));
  renderPeerHistory();
  setStatus("connecting");
  message(`Opening ${remoteId}`);
  state.connectTimer = setTimeout(() => {
    if (state.protocolReady) return;
    connectionFailed(state.connection?.open
      ? `WebRTC opened but the device protocol handshake timed out for ${remoteId}`
      : `Timed out opening WebRTC connection to ${remoteId}`);
  }, 20000);
  const localId = `${product.slug}-web-${Math.floor(Math.random() * 1e9)}`;
  const peer = state.peer = new Peer(localId, {
    host: "0.peerjs.com", port: 443, path: "/", key: "peerjs", secure: true, debug: 1
  });
  peer.on("error", (error) => {
    if (state.peer !== peer) return;
    connectionFailed(errorText(error));
  });
  peer.on("open", () => {
    installCandidateFilter(peer);
    const connection = state.connection = peer.connect(remoteId, {
      serialization: "raw", reliable: true, label: `${product.slug}-editor`
    });
    installAnswerFilter(connection);
    const protocol = state.protocol = new DeviceProtocol((raw) => {
      if (!connection.open) {
        reconnectSession.detach(protocol);
        throw new Error("Connection closed before request was sent");
      }
      connection.send(raw);
    }, { onEvent: traceProtocol });
    connection.on("open", async () => {
      setStatus("verifying");
      message(`WebRTC open; verifying ${remoteId}`);
      try {
        await verifyDeviceProtocol(protocol);
        if (state.protocol !== protocol || !connection.open) return;
        state.protocolReady = true;
        clearTimeout(state.connectTimer);
        state.connectTimer = null;
        setStatus("online", "online");
        reconnectSession.attach(protocol);
        enabled(true);
        message(`${retrying ? "Reconnected" : "Connected"} to ${remoteId}`);
        state.consoleCursor = 0;
        $("#device-console").textContent = "";
        pollConsole();
        await refreshProjects();
      } catch (error) {
        if (state.protocol === protocol)
          connectionFailed(`Device protocol handshake failed: ${error.message}`);
      }
    });
    connection.on("data", async (value) => {
      try { protocol.receive(JSON.parse(await decodeMessage(value))); }
      catch (error) { message(`Invalid device response: ${error.message}`); }
    });
    connection.on("close", () => {
      if (state.connection !== connection) return;
      reconnectSession.detach(protocol);
      protocol.close("Connection lost; waiting to reconnect");
      state.protocol = null;
      setStatus("offline"); enabled(false); stopConsole(); scheduleReconnect();
    });
    connection.on("error", (error) => {
      if (state.connection !== connection) return;
      connectionFailed(errorText(error, "WebRTC data channel failed"));
    });
  });
}

function formatBytes(value) {
  if (value < 1024) return `${value} B`;
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KiB`;
  return `${(value / 1024 / 1024).toFixed(1)} MiB`;
}

function renderAssets() {
  const list = $("#assets");
  list.replaceChildren();
  if (!state.assets.length) {
    const empty = document.createElement("li"); empty.className = "empty"; empty.textContent = "No assets"; list.append(empty); return;
  }
  for (const asset of state.assets) {
    const row = document.createElement("li");
    const info = document.createElement("span"); info.className = "name"; info.textContent = asset.path;
    const size = document.createElement("span"); size.className = "size"; size.textContent = formatBytes(asset.size || 0); info.append(document.createElement("br"), size);
    const actions = document.createElement("span"); actions.className = "asset-actions";
    const download = document.createElement("button"); download.className = "download"; download.textContent = "Download";
    download.onclick = async () => {
      try {
        message(`Retrieving ${asset.path}…`);
        const bytes = await reconnectSession.retry((protocol) =>
          downloadAsset(protocol, state.project, asset.path,
            { onProgress: ({ offset, size }) => message(`Retrieving ${asset.path}… ${formatBytes(offset)} / ${formatBytes(size)}`) }),
        { onRetry: () => message(`Connection interrupted; resuming ${asset.path} after reconnect…`) });
        const url = URL.createObjectURL(new Blob([bytes]));
        const link = document.createElement("a");
        link.href = url; link.download = asset.path.split("/").pop() || "asset";
        document.body.append(link); link.click(); link.remove(); URL.revokeObjectURL(url);
        message(`Downloaded ${asset.path}`);
      } catch (error) { message(`${asset.path}: ${error.message}`); }
    };
    const remove = document.createElement("button"); remove.className = "delete"; remove.textContent = "Delete";
    remove.onclick = async () => {
      if (!confirm(`Delete ${asset.path}?`)) return;
      try { await send("asset.delete", { project: state.project, path: asset.path }); await retrieve(); message(`Deleted ${asset.path}`); }
      catch (error) { message(error.message); }
    };
    actions.append(download, remove); row.append(info, actions); list.append(row);
  }
}

function renderProjects() {
  const picker = $("#project");
  picker.replaceChildren(...state.projects.map((project) => {
    const option = document.createElement("option");
    option.value = project.name; option.textContent = project.metadata?.title || project.name;
    option.selected = project.name === state.project;
    return option;
  }));
}

function renderMetadata() {
  $("#project-title").value = state.metadata.title || state.project || "";
  $("#project-description").value = state.metadata.description || "";
}

function renderRevisions() {
  const list = $("#revisions"); list.replaceChildren();
  if (!state.revisions.length) {
    const empty = document.createElement("li"); empty.className = "empty"; empty.textContent = "No saved revisions"; list.append(empty); return;
  }
  for (const revision of [...state.revisions].reverse()) {
    const row = document.createElement("li"); row.append(document.createTextNode(revision));
    const actions = document.createElement("span"); actions.className = "revision-actions";
    const inspect = document.createElement("button"); inspect.textContent = "Inspect";
    inspect.onclick = async () => {
      try {
        message(`Retrieving ${revision}…`);
        const snapshot = await send("revision.get", { project: state.project, revision });
        state.inspectedRevision = revision;
        $("#revision-name").textContent = revision;
        const title = snapshot.metadata?.title || (snapshot.legacy ? "Legacy code-only revision" : "Untitled project");
        $("#revision-summary").textContent = `${title} · ${(snapshot.assets || []).length} assets`;
        $("#revision-metadata").value = JSON.stringify(snapshot.metadata || {}, null, 2);
        $("#revision-code").value = snapshot.code || "";
        $("#revision-assets").textContent = (snapshot.assets || [])
          .map((asset) => `${asset.path}  ${formatBytes(asset.size || 0)}`).join("\n") || "None";
        $("#revision-dialog").showModal();
        message(`Inspecting ${revision}; current project is unchanged`);
      } catch (error) { message(error.message); }
    };
    const restore = document.createElement("button"); restore.textContent = "Restore";
    restore.onclick = () => restoreRevision(revision, false);
    actions.append(inspect, restore); row.append(actions); list.append(row);
  }
}

async function restoreRevision(revision, run) {
  if (!revision) return;
  const action = run ? "restore and run" : "restore";
  if (!confirm(`${action[0].toUpperCase()}${action.slice(1)} ${revision}? Current code, details, and assets are preserved as a new revision.`)) return;
  try {
    $("#revision-dialog").close();
    await studioActions.restore(revision, run);
    await retrieve();
    message(run ? `${revision} restored and running` : `Restored ${revision}`);
  } catch (error) { message(error.message); }
}

async function refreshProjects() {
  try {
    const response = await send("project.list");
    state.projects = response.projects || [];
    state.project = state.project && state.projects.some((item) => item.name === state.project)
      ? state.project : (response.active || state.projects[0]?.name || "");
    renderProjects();
    if (state.project) await retrieve();
    else message("Connected; create a project to begin");
  } catch (error) { message(error.message); }
}

async function retrieve() {
  try {
    message("Retrieving project…");
    const project = await send("project.get", { project: state.project });
    state.project = project.project || state.project;
    state.metadata = project.metadata || {};
    editor.setValue(project.code || "", -1);
    state.assets = project.assets || [];
    state.revisions = project.revisions || [];
    renderAssets();
    renderRevisions();
    renderMetadata();
    message("Project retrieved");
  } catch (error) { message(error.message); }
}

async function save(run = false) {
  try {
    await studioActions.save(run);
  } catch (error) { message(error.message); }
}

function renderOperation(operation) {
  const labels = {
    saving: "Saving main.js…",
    saved: "main.js saved",
    restoring: "Restoring project snapshot…",
    restored: "Project snapshot restored",
    activating: "Requesting renderer restart…",
    pending: "Waiting for renderer…",
    stopping: "Stopping previous renderer…",
    running: `Project ${operation.project || state.project} is running`,
    failed: operation.detail || "Project failed to start"
  };
  message(labels[operation.state] || `Project state: ${operation.state}`);
  const busy = !["saved", "restored", "running", "failed"].includes(operation.state);
  $("#save").disabled = busy;
  $("#run").disabled = busy;
  $("#operation").textContent = operation.state || "idle";
  $("#operation").dataset.state = operation.state || "idle";
}

const studioActions = createStudioActions({
  protocol: () => state.protocol,
  saveProtocol: () => reconnectSession,
  getProject: () => state.project,
  getCode: () => editor.getValue(),
  onState: renderOperation
});

async function upload(files) {
  const batch = await uploadAssetBatch(files, async (file) => {
    message(`Uploading ${file.name}…`);
    const content = await file.arrayBuffer();
    await reconnectSession.retry((protocol) =>
      uploadAsset(protocol, state.project, file.name, content,
        { onProgress: ({ offset, size }) => message(`Uploading ${file.name}… ${formatBytes(offset)} / ${formatBytes(size)}`) }),
    { onRetry: () => message(`Connection interrupted; resuming ${file.name} after reconnect…`) });
  });
  if (batch.uploaded.length) await retrieve();
  const summary = `${batch.uploaded.length} uploaded` +
    (batch.failed.length ? `; ${batch.failed.length} failed: ${batch.failed.map(({ file }) => file.name).join(", ")}` : "");
  message(summary);
  return batch;
}

$("#connect").onclick = () => connect(false);
$("#disconnect").onclick = () => { closeCurrent(true); message("Disconnected"); };
$("#clear-console").onclick = () => { $("#device-console").textContent = ""; };
$("#clear-trace").onclick = () => { $("#transport-console").textContent = ""; };
$("#system-check").onclick = async () => {
  try {
    message("Running read-only interaction check…");
    await runInteractionCheck(reconnectSession, {
      project: state.project,
      onStage: ({ name, state: result, elapsedMs, detail = "" }) =>
        appendTrace(`CHECK ${name} ${result} ${Math.round(elapsedMs)}ms${detail ? ` ${detail}` : ""}`)
    });
    message("Interaction check passed");
  } catch (error) {
    message(`Interaction check failed: ${error.message}`);
  }
};
$("#revision-restore").onclick = () => restoreRevision(state.inspectedRevision, false);
$("#revision-run").onclick = () => restoreRevision(state.inspectedRevision, true);
$("#project").onchange = async (event) => { state.project = event.target.value; await retrieve(); };
$("#new-project").onclick = async () => {
  const name = prompt("Project name (letters, numbers, dot, underscore or hyphen)")?.trim();
  if (!name) return;
  try {
    await send("project.create", {
      name, metadata: { title: name }, operation: operationToken("project-create")
    });
    state.project = name; await refreshProjects(); message(`Created ${name}`);
  } catch (error) { message(error.message); }
};
$("#retrieve").onclick = retrieve;
$("#save").onclick = () => save(false);
$("#run").onclick = () => save(true);
$("#asset-input").onchange = async (event) => {
  await upload([...event.target.files]);
  event.target.value = "";
};
$("#metadata-form").onsubmit = async (event) => {
  event.preventDefault();
  try {
    const metadata = { ...state.metadata,
      title: $("#project-title").value.trim(),
      description: $("#project-description").value.trim() };
    if (!metadata.title) throw new Error("Project title is required");
    await send("project.metadata.put", { project: state.project, metadata });
    state.metadata = metadata;
    await refreshProjects();
    message("Project details saved");
  } catch (error) { message(error.message); }
};
editor.commands.addCommand({ name: "save", bindKey: { win: "Ctrl-S", mac: "Command-S" }, exec: () => save(false) });
editor.commands.addCommand({ name: "run", bindKey: { win: "Ctrl-Enter", mac: "Command-Enter" }, exec: () => save(true) });
