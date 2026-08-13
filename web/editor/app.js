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

const state = { peer: null, connection: null, connectTimer: null, pending: new Map(), assets: [], negotiation: 0 };
const controls = ["#retrieve", "#save", "#run", "#asset-input"];
const peerIdInput = $("#peer-id");
peerIdInput.value = localStorage.getItem(`${product.slug}.peerId`) || `${product.slug}-demo`;

function setStatus(text, kind = "idle") {
  $("#status").textContent = text;
  $("#status").dataset.state = kind;
}

function message(text) { $("#message").textContent = text; }
function enabled(value) { controls.forEach((id) => { $(id).disabled = !value; }); }
function requestId() { return crypto.randomUUID?.() || `${Date.now()}-${Math.random()}`; }

function errorText(error, fallback = "PeerJS connection failed") {
  return error?.message || error?.type || String(error || fallback) || fallback;
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

function send(type, fields = {}) {
  if (!state.connection?.open) return Promise.reject(new Error("Device is not connected"));
  const id = requestId();
  state.connection.send(JSON.stringify({ id, type, ...fields }));
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      state.pending.delete(id);
      reject(new Error(`${type} timed out`));
    }, 15000);
    state.pending.set(id, { resolve, reject, timer });
  });
}

function onResponse(response) {
  const pending = state.pending.get(response.id);
  if (!pending) return;
  clearTimeout(pending.timer);
  state.pending.delete(response.id);
  response.ok ? pending.resolve(response) : pending.reject(new Error(response.error || "Device error"));
}

function closeCurrent() {
  clearTimeout(state.connectTimer);
  state.connectTimer = null;
  state.connection?.close();
  state.peer?.destroy();
  state.connection = null;
  state.peer = null;
  enabled(false);
}

async function connect() {
  closeCurrent();
  state.negotiation += 1;
  console.info(`NEGOTIATION ${state.negotiation} BROWSER begin`);
  const remoteId = peerIdInput.value.trim();
  if (!remoteId) return;
  localStorage.setItem(`${product.slug}.peerId`, remoteId);
  setStatus("connecting");
  message(`Opening ${remoteId}`);
  state.connectTimer = setTimeout(() => {
    if (state.connection?.open) return;
    setStatus("error", "error");
    message(`Timed out opening WebRTC connection to ${remoteId}`);
    closeCurrent();
  }, 20000);
  const localId = `${product.slug}-web-${Math.floor(Math.random() * 1e9)}`;
  const peer = state.peer = new Peer(localId, {
    host: "0.peerjs.com", port: 443, path: "/", key: "peerjs", secure: true, debug: 1
  });
  peer.on("error", (error) => {
    clearTimeout(state.connectTimer);
    state.connectTimer = null;
    setStatus("error", "error");
    message(errorText(error));
  });
  peer.on("open", () => {
    installCandidateFilter(peer);
    const connection = state.connection = peer.connect(remoteId, {
      serialization: "raw", reliable: true, label: `${product.slug}-editor`
    });
    installAnswerFilter(connection);
    connection.on("open", async () => {
      clearTimeout(state.connectTimer);
      state.connectTimer = null;
      setStatus("online", "online");
      enabled(true);
      message(`Connected to ${remoteId}`);
      await retrieve();
    });
    connection.on("data", async (value) => {
      try { onResponse(JSON.parse(await decodeMessage(value))); }
      catch (error) { message(`Invalid device response: ${error.message}`); }
    });
    connection.on("close", () => { setStatus("offline"); enabled(false); message("Connection closed"); });
    connection.on("error", (error) => {
      clearTimeout(state.connectTimer);
      state.connectTimer = null;
      setStatus("error", "error");
      message(errorText(error, "WebRTC data channel failed"));
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
        const response = await send("asset.get", { path: asset.path });
        const bytes = fromBase64(response.content || "");
        if (Number(response.size) !== bytes.byteLength) throw new Error("Asset size mismatch");
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
      try { await send("asset.delete", { path: asset.path }); await retrieve(); message(`Deleted ${asset.path}`); }
      catch (error) { message(error.message); }
    };
    actions.append(download, remove); row.append(info, actions); list.append(row);
  }
}

async function retrieve() {
  try {
    message("Retrieving project…");
    const project = await send("project.get");
    editor.setValue(project.code || "", -1);
    state.assets = project.assets || [];
    renderAssets();
    message("Project retrieved");
  } catch (error) { message(error.message); }
}

async function save(run = false) {
  try {
    message(run ? "Saving and activating…" : "Saving…");
    await send("code.put", { content: editor.getValue() });
    if (run) await send("project.activate");
    message(run ? "Project activated" : "main.js saved");
  } catch (error) { message(error.message); }
}

function toBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  return btoa(binary);
}

function fromBase64(value) {
  const binary = atob(value);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index++) bytes[index] = binary.charCodeAt(index);
  return bytes;
}

async function upload(files) {
  for (const file of files) {
    try {
      message(`Uploading ${file.name}…`);
      await send("asset.put", { path: file.name, content: toBase64(await file.arrayBuffer()) });
    } catch (error) { message(`${file.name}: ${error.message}`); return; }
  }
  await retrieve();
  message(`${files.length} asset${files.length === 1 ? "" : "s"} uploaded`);
}

$("#connect").onclick = connect;
$("#retrieve").onclick = retrieve;
$("#save").onclick = () => save(false);
$("#run").onclick = () => save(true);
$("#asset-input").onchange = (event) => upload([...event.target.files]);
editor.commands.addCommand({ name: "save", bindKey: { win: "Ctrl-S", mac: "Command-S" }, exec: () => save(false) });
editor.commands.addCommand({ name: "run", bindKey: { win: "Ctrl-Enter", mac: "Command-Enter" }, exec: () => save(true) });
