import assert from "node:assert/strict";
import test from "node:test";
import { runInteractionCheck, verifyDeviceProtocol } from "../interaction-check.js";

test("connection verification requires a real versioned device response", async () => {
  await assert.rejects(
    verifyDeviceProtocol({ request: async () => ({ type: "ack", protocolVersion: 2 }) }),
    /unexpected handshake response/);
  await assert.rejects(
    verifyDeviceProtocol({ request: async () => ({ type: "system.pong", protocolVersion: 1 }) }),
    /unsupported device protocol 1/);
  const response = await verifyDeviceProtocol({
    request: async (type) => ({ type: type === "system.ping" ? "system.pong" : "", protocolVersion: 2 })
  });
  assert.equal(response.protocolVersion, 2);
});

test("read-only interaction check covers transport, projects, retrieval, and console", async () => {
  const requests = [];
  const stages = [];
  const protocol = {
    async request(type, fields = {}) {
      requests.push({ type, ...fields });
      if (type === "system.ping") return {
        type: "system.pong", protocolVersion: 2, activeProject: "demo"
      };
      if (type === "project.list") return {
        type: "projects", active: "demo", projects: [{ name: "demo" }]
      };
      if (type === "project.get") return {
        type: "project", project: fields.project, code: "update=()=>{}", assets: []
      };
      if (type === "console.get") return { type: "console", cursor: 0, content: "" };
      throw new Error(`unexpected ${type}`);
    }
  };
  const result = await runInteractionCheck(protocol, {
    project: "demo", onStage: (stage) => stages.push(stage)
  });
  assert.deepEqual(requests.map(({ type }) => type), [
    "system.ping", "project.list", "project.get", "console.get"
  ]);
  assert.equal(result.project.project, "demo");
  assert.deepEqual(stages.filter(({ state }) => state === "passed").map(({ name }) => name), [
    "protocol handshake", "project list", "project retrieve", "console read"
  ]);
});

test("interaction check identifies the failing stage", async () => {
  const stages = [];
  const protocol = {
    async request(type) {
      if (type === "system.ping") return { type: "system.pong", protocolVersion: 2 };
      if (type === "project.list") throw new Error("radio link lost");
      throw new Error("unexpected request");
    }
  };
  await assert.rejects(runInteractionCheck(protocol, { onStage: (stage) => stages.push(stage) }),
    /project list: radio link lost/);
  assert.deepEqual(stages.at(-1), {
    name: "project list", state: "failed", elapsedMs: stages.at(-1).elapsedMs,
    detail: "radio link lost"
  });
});
