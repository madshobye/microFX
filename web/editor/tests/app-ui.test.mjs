import assert from "node:assert/strict";
import test from "node:test";

class FakeElement {
  constructor(tag = "div") {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.dataset = {};
    this.disabled = false;
    this.files = [];
    this.open = false;
    this.scrollHeight = 0;
    this.scrollTop = 0;
    this.textContent = "";
    this.value = "";
  }

  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  remove() {}
  click() { return this.onclick?.({ target: this }); }
  close() { this.open = false; }
  showModal() { this.open = true; }
}

class FakeStorage {
  constructor() { this.values = new Map(); }
  getItem(key) { return this.values.has(key) ? this.values.get(key) : null; }
  setItem(key, value) { this.values.set(key, String(value)); }
}

class Emitter {
  constructor() { this.handlers = new Map(); }
  on(name, handler) { this.handlers.set(name, handler); }
  emit(name, value) { this.handlers.get(name)?.(value); }
}

const server = {
  requests: [],
  active: "demo",
  projects: new Map([
    ["demo", {
      metadata: { title: "Demo", description: "Browser harness" },
      code: "fx.configure({ targetFps: 30 });",
      assets: new Map([["keep.bin", Uint8Array.of(1, 2, 3)]]),
      revisions: new Map([["r000001", {
        metadata: { title: "Earlier Demo", description: "Snapshot" },
        code: "fx.configure({ targetFps: 24 });",
        assets: new Map([["old.bin", Uint8Array.of(9, 8)]])
      }]])
    }]
  ]),
  upload: [],
  deferPing: false,
  pendingPing: null,
  handle(raw, connection) {
    const request = JSON.parse(raw);
    this.requests.push(request);
    const response = { id: request.id, ok: true };
    if (request.type === "system.ping") Object.assign(response, {
      type: "system.pong", protocolVersion: 2, activeProject: this.active,
      persistenceReady: true, rendererState: "running", rendererProject: this.active
    });
    if (request.type === "project.list") Object.assign(response, {
      active: this.active,
      projects: [...this.projects].map(([name, project]) => ({ name, metadata: project.metadata }))
    });
    const project = this.projects.get(request.project);
    if (request.type === "project.get") Object.assign(response, {
      project: request.project,
      metadata: project.metadata,
      code: project.code,
      assets: [...project.assets].map(([path, bytes]) => ({ path, size: bytes.length })),
      folders: project.folders || [],
      revisions: [...project.revisions.keys()]
    });
    if (request.type === "project.create") {
      this.projects.set(request.name, {
        metadata: request.metadata,
        code: "",
        assets: new Map(),
        folders: [],
        revisions: new Map()
      });
      Object.assign(response, { project: request.name, operation: request.operation });
    }
    if (request.type === "project.metadata.put") project.metadata = request.metadata;
    if (request.type === "code.put") project.code = request.content;
    if (request.type === "revision.get") {
      const revision = project.revisions.get(request.revision);
      Object.assign(response, {
        project: request.project,
        revision: request.revision,
        metadata: revision.metadata,
        code: revision.code,
        assets: [...revision.assets].map(([path, bytes]) => ({ path, size: bytes.length }))
      });
    }
    if (request.type === "revision.restore") {
      const revision = project.revisions.get(request.revision);
      project.metadata = structuredClone(revision.metadata);
      project.code = revision.code;
      project.assets = new Map([...revision.assets].map(([path, bytes]) => [path, bytes.slice()]));
    }
    if (request.type === "asset.upload.status") {
      this.upload = [];
      Object.assign(response, { offset: 0, size: request.size });
    }
    if (request.type === "asset.upload.chunk") {
      assert.equal(request.offset, this.upload.length);
      this.upload.push(...Uint8Array.from(atob(request.content), value => value.charCodeAt(0)));
      Object.assign(response, { offset: this.upload.length, size: request.size });
    }
    if (request.type === "asset.upload.commit") {
      project.assets.set(request.path, Uint8Array.from(this.upload));
      Object.assign(response, { project: request.project, path: request.path, size: request.size });
    }
    if (request.type === "asset.get.chunk") {
      const bytes = project.assets.get(request.path);
      const chunk = bytes.subarray(request.offset);
      Object.assign(response, {
        project: request.project,
        path: request.path,
        offset: request.offset,
        next: request.offset + chunk.length,
        size: bytes.length,
        eof: true,
        content: btoa(String.fromCharCode(...chunk))
      });
    }
    if (request.type === "asset.delete") project.assets.delete(request.path);
    if (request.type === "asset.folder.create") {
      project.folders ||= [];
      if (!project.folders.includes(request.path)) project.folders.push(request.path);
      Object.assign(response, { project: request.project, path: request.path });
    }
    if (request.type === "console.get") Object.assign(response, { cursor: 0, content: "" });
    if (request.type === "project.save-run") {
      project.code = request.content;
      response.activation = request.activation;
    }
    if (request.type === "project.activate") response.activation = request.activation;
    if (request.type === "project.status") Object.assign(response, {
      activation: request.activation, project: request.project || this.active, state: "running"
    });
    const deliver = () => connection.emit("data", JSON.stringify(response));
    if (request.type === "system.ping" && this.deferPing) {
      this.pendingPing = deliver;
      return;
    }
    queueMicrotask(deliver);
  }
};

class FakeConnection extends Emitter {
  constructor() { super(); this.open = false; }
  send(raw) { server.handle(raw, this); }
  close() {
    if (!this.open) return;
    this.open = false;
    this.emit("close");
  }
}

class FakePeer extends Emitter {
  static instances = [];

  constructor(id) {
    super();
    this.id = id;
    this.socket = { send() {} };
    FakePeer.instances.push(this);
    setTimeout(() => this.emit("open", id), 0);
  }

  connect() {
    this.connection = new FakeConnection();
    setTimeout(() => {
      this.connection.open = true;
      this.connection.emit("open");
    }, 0);
    return this.connection;
  }

  destroy() { this.destroyed = true; }
}

function browserEnvironment() {
  const ids = [
    "asset-input", "asset-folder-input", "asset-folder", "create-asset-folder", "assets",
    "clear-console", "clear-trace", "connect", "console", "device-console",
    "disconnect", "editor", "message", "metadata-form", "new-project", "operation",
    "peer-history", "peer-id", "product-title", "project", "project-description",
    "project-title", "retrieve", "revision-assets", "revision-code", "revision-dialog",
    "revision-metadata", "revision-name", "revision-restore", "revision-run",
    "revision-summary", "revisions", "run", "save", "save-metadata", "status",
    "system-check", "transport-console"
  ];
  const elements = new Map(ids.map((id) => [`#${id}`, new FakeElement()]));
  elements.get("#peer-id").value = "";
  const body = new FakeElement("body");
  globalThis.document = {
    title: "",
    body,
    querySelector: (selector) => elements.get(selector),
    createElement: (tag) => new FakeElement(tag),
    createTextNode: (text) => ({ textContent: text })
  };
  globalThis.localStorage = new FakeStorage();
  globalThis.location = {search: "?peer=microfx-query-device"};
  globalThis.Peer = FakePeer;
  globalThis.confirm = () => true;
  globalThis.prompt = () => null;
  globalThis.URL.createObjectURL = () => "blob:asset";
  globalThis.URL.revokeObjectURL = () => {};
  const editor = {
    value: "",
    session: { setMode() {}, setUseSoftTabs() {}, setTabSize() {} },
    commands: { addCommand() {} },
    setTheme() {}, setOptions() {},
    setValue(value) { this.value = value; },
    getValue() { return this.value; }
  };
  globalThis.ace = { edit: () => editor };
  return { elements, editor };
}

async function until(predicate, message, timeoutMs = 2000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) return;
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.fail(message);
}

test("the real Studio page controls the complete project, asset, revision, and connection workflow", async (context) => {
  const { elements, editor } = browserEnvironment();
  await import(`../app.js?ui-test=${Date.now()}`);
  context.after(() => {
    if (elements.get("#status").textContent !== "offline") elements.get("#disconnect").click();
  });

  assert.equal(elements.get("#peer-id").value, "microfx-query-device");
  server.deferPing = true;
  elements.get("#connect").click();
  await until(() => elements.get("#status").textContent === "verifying",
    "Studio did not enter protocol verification");
  assert.equal(elements.get("#run").disabled, true,
    "Save & Run must remain disabled before the application protocol responds");
  server.deferPing = false;
  server.pendingPing?.();
  server.pendingPing = null;
  await until(() => elements.get("#status").textContent === "online", "Studio did not connect");
  await until(() => elements.get("#message").textContent === "Project retrieved",
    "Studio did not retrieve the selected project");

  assert.equal(editor.value, "fx.configure({ targetFps: 30 });");
  assert.equal(elements.get("#project").children[0].value, "demo");
  assert.equal(elements.get("#project-title").value, "Demo");
  assert.equal(elements.get("#run").disabled, false);
  assert.equal(server.requests[0].type, "system.ping");
  assert.match(elements.get("#transport-console").textContent,
    /RECEIVED system\.ping .* ok → system\.pong/);

  await elements.get("#system-check").click();
  assert.equal(elements.get("#message").textContent, "Interaction check passed");
  assert.match(elements.get("#transport-console").textContent, /CHECK project retrieve passed/);

  editor.value = "fx.configure({ targetFps: 60 });";
  elements.get("#run").click();
  await until(() => elements.get("#operation").textContent === "running",
    "Save & Run did not reach renderer health");

  const saveRun = server.requests.find((request) => request.type === "project.save-run");
  const status = server.requests.find((request) => request.type === "project.status");
  assert.deepEqual({ project: saveRun.project, content: saveRun.content },
    { project: "demo", content: "fx.configure({ targetFps: 60 });" });
  assert.ok(saveRun.activation, "Save & Run must use a stable activation identity");
  assert.equal(status.activation, saveRun.activation);
  assert.equal(elements.get("#message").textContent, "Project demo is running");

  const uploadTarget = { files: [{
    name: "new.bin",
    async arrayBuffer() { return Uint8Array.of(4, 5, 6, 7).buffer; }
  }], value: "selected" };
  await elements.get("#asset-input").onchange({ target: uploadTarget });
  assert.equal(uploadTarget.value, "");
  const demo = server.projects.get("demo");
  assert.deepEqual([...demo.assets.keys()].sort(), ["keep.bin", "new.bin"]);
  assert.deepEqual(demo.assets.get("keep.bin"), Uint8Array.of(1, 2, 3));
  assert.deepEqual(demo.assets.get("new.bin"), Uint8Array.of(4, 5, 6, 7));
  assert.equal(server.requests.filter(request => request.type === "asset.upload.commit").length, 1);
  assert.equal(server.requests.some(request => request.type === "asset.delete"), false);
  assert.equal(elements.get("#assets").children.length, 2);
  assert.equal(elements.get("#message").textContent, "1 uploaded");

  const keepActions = elements.get("#assets").children[0].children[1];
  await keepActions.children[0].click();
  assert.equal(server.requests.at(-1).type, "asset.get.chunk");
  assert.equal(elements.get("#message").textContent, "Downloaded keep.bin");

  const newActions = elements.get("#assets").children[1].children[1];
  await newActions.children[1].click();
  assert.deepEqual([...demo.assets.keys()], ["keep.bin"]);
  assert.equal(elements.get("#assets").children.length, 1);
  assert.equal(elements.get("#message").textContent, "Deleted new.bin");

  elements.get("#project-title").value = "Renamed Demo";
  elements.get("#project-description").value = "Updated from the real form";
  await elements.get("#metadata-form").onsubmit({ preventDefault() {} });
  assert.deepEqual(demo.metadata, {
    title: "Renamed Demo", description: "Updated from the real form"
  });
  assert.equal(elements.get("#message").textContent, "Project details saved");

  editor.value = "fx.configure({ targetFps: 25 });";
  await elements.get("#save").click();
  assert.equal(demo.code, "fx.configure({ targetFps: 25 });");
  assert.equal(elements.get("#operation").textContent, "saved");

  globalThis.prompt = () => "fresh-project";
  await elements.get("#new-project").click();
  assert.ok(server.projects.has("fresh-project"));
  assert.equal(elements.get("#project-title").value, "fresh-project");
  assert.equal(elements.get("#message").textContent, "Created fresh-project");

  await elements.get("#project").onchange({ target: { value: "demo" } });
  assert.equal(elements.get("#project-title").value, "Renamed Demo");
  assert.equal(elements.get("#revisions").children.length, 1);

  const revisionActions = elements.get("#revisions").children[0].children[1];
  await revisionActions.children[0].click();
  assert.equal(elements.get("#revision-dialog").open, true);
  assert.equal(elements.get("#revision-name").textContent, "r000001");
  assert.match(elements.get("#revision-code").value, /targetFps: 24/);
  assert.match(elements.get("#revision-assets").textContent, /old\.bin/);
  assert.equal(elements.get("#message").textContent,
    "Inspecting r000001; current project is unchanged");

  await elements.get("#revision-restore").click();
  assert.equal(demo.code, "fx.configure({ targetFps: 24 });");
  assert.equal(elements.get("#project-title").value, "Earlier Demo");
  assert.equal(elements.get("#message").textContent, "Restored r000001");

  await revisionActions.children[0].click();
  await elements.get("#revision-run").click();
  assert.equal(elements.get("#operation").textContent, "running");
  assert.equal(elements.get("#message").textContent, "r000001 restored and running");

  elements.get("#device-console").textContent = "renderer log";
  elements.get("#clear-console").click();
  assert.equal(elements.get("#device-console").textContent, "");

  elements.get("#disconnect").click();
  assert.equal(elements.get("#status").textContent, "offline");
  assert.equal(elements.get("#message").textContent, "Disconnected");
  assert.equal(elements.get("#run").disabled, true);
  assert.equal(FakePeer.instances.at(-1).destroyed, true);
});
