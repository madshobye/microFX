import assert from "node:assert/strict";
import test from "node:test";
import { createStudioActions } from "../actions.js";
import { ReconnectSession } from "../reconnect-session.js";

function controlledProtocol() {
  const requests = [];
  return {
    requests,
    request(type, fields) {
      return new Promise((resolve, reject) => requests.push({ type, fields, resolve, reject }));
    }
  };
}

test("the Studio Save & Run action exposes lifecycle and prevents double submission", async () => {
  const protocol = controlledProtocol();
  const states = [];
  const actions = createStudioActions({
    protocol: () => protocol,
    getProject: () => "demo",
    getCode: () => "update = () => {};",
    onState: (state) => states.push(state.state)
  });

  const running = actions.save(true);
  assert.equal(actions.busy, true);
  assert.equal(protocol.requests[0].type, "project.save-run");
  assert.equal(protocol.requests[0].fields.content, "update = () => {};");
  await assert.rejects(actions.save(true), /already running/);
  protocol.requests[0].resolve({ ok: true, activation: protocol.requests[0].fields.activation });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(protocol.requests[1].type, "project.status");
  protocol.requests[1].resolve({ ok: true, state: "running", project: "demo" });
  assert.equal((await running).state, "running");
  assert.equal(actions.busy, false);
  assert.deepEqual(states, ["saving", "activating", "running"]);
});

test("the Studio action reports a renderer failure", async () => {
  const protocol = controlledProtocol();
  const states = [];
  const actions = createStudioActions({
    protocol: () => protocol,
    getProject: () => "broken",
    getCode: () => "throw new Error('broken');",
    onState: (state) => states.push(state)
  });
  const running = actions.save(true);
  protocol.requests[0].resolve({ ok: true, activation: protocol.requests[0].fields.activation });
  await new Promise((resolve) => setImmediate(resolve));
  protocol.requests[1].resolve({ ok: true, state: "failed", detail: "script error" });
  await assert.rejects(running, /script error/);
  assert.equal(states.at(-1).state, "failed");
  assert.match(states.at(-1).detail, /script error/);
});

test("a revision can be restored without activating it", async () => {
  const protocol = controlledProtocol();
  const states = [];
  const actions = createStudioActions({
    protocol: () => protocol,
    getProject: () => "demo",
    getCode: () => "",
    onState: (state) => states.push(state.state)
  });
  const restored = actions.restore("r000003", false);
  assert.equal(protocol.requests[0].type, "revision.restore");
  assert.deepEqual(protocol.requests[0].fields,
    { project: "demo", revision: "r000003" });
  protocol.requests[0].resolve({ ok: true });
  assert.equal((await restored).state, "restored");
  assert.deepEqual(states, ["restoring", "restored"]);
});

test("Restore & Run waits for renderer health and blocks concurrent saves", async () => {
  const protocol = controlledProtocol();
  const states = [];
  const actions = createStudioActions({
    protocol: () => protocol,
    getProject: () => "demo",
    getCode: () => "new code",
    onState: (state) => states.push(state.state)
  });
  const running = actions.restore("r000004", true);
  await assert.rejects(actions.save(), /already running/);
  protocol.requests[0].resolve({ ok: true });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(protocol.requests[1].type, "project.activate");
  const activation = protocol.requests[1].fields.activation;
  assert.ok(activation);
  protocol.requests[1].resolve({ ok: true, activation });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(protocol.requests[2].type, "project.status");
  protocol.requests[2].resolve({ ok: true, state: "running", project: "demo" });
  assert.equal((await running).state, "running");
  assert.deepEqual(states, ["restoring", "activating", "running"]);
});

test("Restore & Run resumes restore and activation on replacement transports", async () => {
  const first = controlledProtocol();
  const second = controlledProtocol();
  const third = controlledProtocol();
  const session = new ReconnectSession({ timeoutMs: 1000 });
  session.attach(first);
  const actions = createStudioActions({
    protocol: () => session.protocol,
    saveProtocol: () => session,
    getProject: () => "demo",
    getCode: () => ""
  });

  const running = actions.restore("r000004", true);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(first.requests[0].type, "revision.restore");
  session.detach(first);
  first.requests[0].reject(new Error("restore acknowledgement lost"));
  session.attach(second);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(second.requests[0].type, "revision.restore");
  assert.deepEqual(second.requests[0].fields, { project: "demo", revision: "r000004" });
  second.requests[0].resolve({ ok: true });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(second.requests[1].type, "project.activate");
  const activation = second.requests[1].fields.activation;
  assert.ok(activation);

  session.detach(second);
  second.requests[1].reject(new Error("activation acknowledgement lost"));
  session.attach(third);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(third.requests[0].type, "project.activate");
  assert.equal(third.requests[0].fields.activation, activation);
  third.requests[0].resolve({ ok: true, activation });
  await new Promise((resolve) => setImmediate(resolve));
  third.requests[1].resolve({ ok: true, state: "running", project: "demo" });
  assert.equal((await running).state, "running");
});

test("Save & Run replays one stable transaction on a replacement transport", async () => {
  const first = controlledProtocol();
  const second = controlledProtocol();
  const third = controlledProtocol();
  const session = new ReconnectSession({ timeoutMs: 1000 });
  session.attach(first);
  const actions = createStudioActions({
    protocol: () => session.protocol,
    saveProtocol: () => session,
    getProject: () => "demo",
    getCode: () => "fx.circle(20,20,10);"
  });

  const running = actions.save(true);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(first.requests[0].type, "project.save-run");
  const activation = first.requests[0].fields.activation;
  assert.ok(activation);
  session.detach(first);
  first.requests[0].reject(new Error("transaction acknowledgement lost"));
  session.attach(second);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(second.requests[0].type, "project.save-run");
  assert.equal(second.requests[0].fields.activation, activation);

  session.detach(second);
  second.requests[0].reject(new Error("second acknowledgement lost"));
  session.attach(third);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(third.requests[0].type, "project.save-run");
  assert.equal(third.requests[0].fields.activation, activation);
  third.requests[0].resolve({ ok: true, activation });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(third.requests[1].type, "project.status");
  assert.equal(third.requests[1].fields.activation, activation);
  third.requests[1].resolve({ ok: true, state: "running", project: "demo" });
  assert.equal((await running).state, "running");
});
