import assert from "node:assert/strict";
import test from "node:test";
import { DeviceProtocol, downloadAsset, saveProject, uploadAsset } from "../protocol.js";

function harness(timeoutMs = 50) {
  const messages = [];
  let sequence = 0;
  const protocol = new DeviceProtocol((raw) => messages.push(JSON.parse(raw)), {
    timeoutMs,
    createId: () => `request-${++sequence}`
  });
  return { protocol, messages };
}

test("correlates out-of-order replies", async () => {
  const { protocol, messages } = harness();
  const first = protocol.request("project.list");
  const second = protocol.request("project.get", { project: "demo" });
  assert.equal(messages.length, 2);
  protocol.receive({ id: messages[1].id, ok: true, value: 2 });
  protocol.receive({ id: messages[0].id, ok: true, value: 1 });
  assert.equal((await first).value, 1);
  assert.equal((await second).value, 2);
});

test("rejects device errors and timeouts", async () => {
  const { protocol, messages } = harness(10);
  const failed = protocol.request("asset.get");
  protocol.receive({ id: messages[0].id, ok: false, error: "missing asset" });
  await assert.rejects(failed, /missing asset/);
  await assert.rejects(protocol.request("project.get"), /project\.get timed out/);
});

test("disconnect rejects every pending interaction", async () => {
  const { protocol } = harness();
  const pending = protocol.request("project.get");
  protocol.close("connection closed");
  await assert.rejects(pending, /connection closed/);
  assert.equal(protocol.pending.size, 0);
});

test("reports correlated protocol lifecycle events without logging payloads", async () => {
  const events = [];
  const messages = [];
  const protocol = new DeviceProtocol((raw) => messages.push(JSON.parse(raw)), {
    createId: () => "trace-1",
    onEvent: (event) => events.push(event)
  });
  const pending = protocol.request("project.save-run", {
    project: "demo", content: "private source", activation: "activation-1"
  });
  protocol.receive({ id: "trace-1", type: "project.activation", ok: true,
    activation: "activation-1" });
  await pending;
  assert.deepEqual(events.map(({ phase, id, type }) => ({ phase, id, type })), [
    { phase: "sent", id: "trace-1", type: "project.save-run" },
    { phase: "received", id: "trace-1", type: "project.save-run" }
  ]);
  assert.equal(events.some((event) => JSON.stringify(event).includes("private source")), false);
});

test("asset helpers use bounded resumable chunks in both directions", async () => {
  const source = new Uint8Array(150000);
  for (let index = 0; index < source.length; index++) source[index] = index % 251;
  const stored = [];
  const requests = [];
  const protocol = {
    async request(type, fields) {
      requests.push({ type, ...fields });
      if (type === "asset.upload.status") return { ok: true, offset: stored.length };
      if (type === "asset.upload.chunk") {
        assert.equal(fields.offset, stored.length);
        const chunk = Uint8Array.from(atob(fields.content), (value) => value.charCodeAt(0));
        assert.ok(chunk.length <= 32768);
        stored.push(...chunk);
        return { ok: true, offset: stored.length };
      }
      if (type === "asset.upload.commit") return { ok: true, size: stored.length };
      if (type === "asset.get.chunk") {
        const chunk = source.subarray(fields.offset, Math.min(fields.offset + 37000, source.length));
        let binary = "";
        for (const value of chunk) binary += String.fromCharCode(value);
        return { ok: true, offset: fields.offset, next: fields.offset + chunk.length,
          size: source.length, eof: fields.offset + chunk.length === source.length,
          content: btoa(binary) };
      }
      throw new Error(`unexpected ${type}`);
    }
  };
  await uploadAsset(protocol, "demo", "large.bin", source, { chunkSize: 32768 });
  assert.deepEqual(Uint8Array.from(stored), source);
  assert.ok(requests.filter(({ type }) => type === "asset.upload.chunk").length > 1);
  const downloaded = await downloadAsset(protocol, "demo", "large.bin");
  assert.deepEqual(downloaded, source);
});

test("Save & Run is one idempotent device transaction", async () => {
  const { protocol, messages } = harness();
  let releasePoll;
  const operation = saveProject(protocol, "kinetic", "fx.circle(1,2,3);", true, {
    activation: "stable-activation", pollMs: 0,
    delay: () => new Promise((resolve) => { releasePoll = resolve; })
  });
  assert.deepEqual(messages[0], {
    id: "request-1", type: "project.save-run", project: "kinetic",
    content: "fx.circle(1,2,3);", activation: "stable-activation"
  });
  protocol.receive({ id: "request-1", ok: true, activation: "stable-activation" });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(messages[1], {
    id: "request-2", type: "project.status", activation: "stable-activation"
  });
  protocol.receive({ id: "request-2", ok: true, state: "stopping" });
  await new Promise((resolve) => setImmediate(resolve));
  releasePoll();
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(messages[2], {
    id: "request-3", type: "project.status", activation: "stable-activation"
  });
  protocol.receive({ id: "request-3", ok: true, state: "running", project: "kinetic" });
  assert.equal((await operation).project, "kinetic");
});

test("Save & Run reports renderer startup failure", async () => {
  const { protocol, messages } = harness();
  const operation = saveProject(protocol, "broken", "throw Error('bad');", true, {
    activation: "failed-activation", pollMs: 0, delay: () => Promise.resolve()
  });
  protocol.receive({ id: messages[0].id, ok: true, activation: "failed-activation" });
  await new Promise((resolve) => setImmediate(resolve));
  protocol.receive({ id: messages[1].id, ok: true, state: "failed", detail: "script error" });
  await assert.rejects(operation, /script error/);
});

test("plain Save never activates", async () => {
  const { protocol, messages } = harness();
  const operation = saveProject(protocol, "demo", "update=()=>{};", false);
  protocol.receive({ id: messages[0].id, ok: true });
  await operation;
  assert.equal(messages.length, 1);
  assert.equal(messages[0].type, "code.put");
});
