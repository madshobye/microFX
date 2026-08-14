import assert from "node:assert/strict";
import test from "node:test";
import { ReconnectSession } from "../reconnect-session.js";

test("a retryable operation resumes on the replacement protocol", async () => {
  const session = new ReconnectSession({ timeoutMs: 100 });
  const first = { name: "first" };
  const second = { name: "second" };
  session.attach(first);
  let release;
  const operation = session.retry(async (protocol, attempt) => {
    if (attempt === 1) {
      await new Promise((resolve) => { release = resolve; });
      throw new Error("old channel closed");
    }
    return protocol.name;
  });
  await new Promise((resolve) => setImmediate(resolve));
  session.detach(first);
  release();
  setImmediate(() => session.attach(second));
  assert.equal(await operation, "second");
});

test("a request can be replayed on the replacement protocol", async () => {
  const requests = [];
  const first = { request: (type, fields) => new Promise((resolve, reject) =>
    requests.push({ transport: "first", type, fields, resolve, reject })) };
  const second = { request: (type, fields) => new Promise((resolve, reject) =>
    requests.push({ transport: "second", type, fields, resolve, reject })) };
  const session = new ReconnectSession({ timeoutMs: 100 });
  session.attach(first);
  const response = session.request("project.get", { project: "demo" });
  await new Promise((resolve) => setImmediate(resolve));
  session.detach(first);
  requests[0].reject(new Error("channel closed"));
  session.attach(second);
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(requests.map(({ transport, type, fields }) => ({ transport, type, fields })), [
    { transport: "first", type: "project.get", fields: { project: "demo" } },
    { transport: "second", type: "project.get", fields: { project: "demo" } }
  ]);
  requests[1].resolve({ ok: true, project: "demo" });
  assert.equal((await response).project, "demo");
});

test("device errors on the current protocol are not retried", async () => {
  const session = new ReconnectSession();
  session.attach({ name: "current" });
  let attempts = 0;
  await assert.rejects(session.retry(async () => {
    attempts += 1;
    throw new Error("invalid asset path");
  }), /invalid asset path/);
  assert.equal(attempts, 1);
});

test("intentional disconnect cancels operations waiting for reconnect", async () => {
  const session = new ReconnectSession({ timeoutMs: 100 });
  const waiting = session.retry(async () => "unexpected");
  session.cancel("Disconnected by user");
  await assert.rejects(waiting, /Disconnected by user/);
});

test("waiting for a reconnect has a bounded timeout", async () => {
  const session = new ReconnectSession({ timeoutMs: 5 });
  await assert.rejects(session.wait(), /Timed out waiting for device reconnection/);
});
