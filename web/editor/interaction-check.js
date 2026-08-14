function now() {
  return globalThis.performance?.now?.() ?? Date.now();
}

async function stage(name, operation, onStage) {
  const started = now();
  onStage({ name, state: "running", elapsedMs: 0 });
  try {
    const value = await operation();
    const result = { name, state: "passed", elapsedMs: Math.max(0, now() - started) };
    onStage(result);
    return value;
  } catch (error) {
    const result = {
      name,
      state: "failed",
      elapsedMs: Math.max(0, now() - started),
      detail: error?.message || String(error)
    };
    onStage(result);
    throw new Error(`${name}: ${result.detail}`);
  }
}

export async function verifyDeviceProtocol(protocol) {
  const response = await protocol.request("system.ping");
  if (response?.type !== "system.pong")
    throw new Error(`unexpected handshake response ${response?.type || "<missing>"}`);
  if (Number(response.protocolVersion) !== 2)
    throw new Error(`unsupported device protocol ${response?.protocolVersion ?? "<missing>"}`);
  return response;
}

// This pass is deliberately read-only. Save & Run remains a separate explicit
// action because connecting a diagnostic must never restart a user's renderer.
export async function runInteractionCheck(protocol, options = {}) {
  const onStage = options.onStage ?? (() => {});
  const summary = {};
  summary.system = await stage("protocol handshake",
    () => verifyDeviceProtocol(protocol), onStage);
  summary.projects = await stage("project list",
    () => protocol.request("project.list"), onStage);
  const project = options.project || summary.projects.active || summary.projects.projects?.[0]?.name || "";
  if (project) {
    summary.project = await stage("project retrieve",
      () => protocol.request("project.get", { project }), onStage);
  }
  summary.console = await stage("console read",
    () => protocol.request("console.get", { cursor: 0 }), onStage);
  return summary;
}
