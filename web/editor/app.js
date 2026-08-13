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

const state = { peer: null, connection: null, pending: new Map(), assets: [] };
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
  state.connection?.close();
  state.peer?.destroy();
  state.connection = null;
  state.peer = null;
  enabled(false);
}

async function connect() {
  closeCurrent();
  const remoteId = peerIdInput.value.trim();
  if (!remoteId) return;
  localStorage.setItem(`${product.slug}.peerId`, remoteId);
  setStatus("connecting");
  message(`Opening ${remoteId}`);
  const localId = `${product.slug}-web-${Math.floor(Math.random() * 1e9)}`;
  const peer = state.peer = new Peer(localId, {
    host: "0.peerjs.com", port: 443, path: "/", key: "peerjs", secure: true, debug: 0
  });
  peer.on("error", (error) => { setStatus("error", "error"); message(error.message); });
  peer.on("open", () => {
    const connection = state.connection = peer.connect(remoteId, {
      serialization: "raw", reliable: true, label: `${product.slug}-editor`
    });
    connection.on("open", async () => {
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
    connection.on("error", (error) => { setStatus("error", "error"); message(error.message); });
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
