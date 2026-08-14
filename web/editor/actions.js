import { operationToken, saveProject, waitForActivation } from "./protocol.js";

export function createStudioActions({ protocol, saveProtocol = protocol,
  getProject, getCode, onState = () => {} }) {
  let operation = null;

  async function save(run = false) {
    if (operation) throw new Error("A save operation is already running");
    operation = saveProject(saveProtocol(), getProject(), getCode(), run, { onState });
    try {
      return await operation;
    } catch (error) {
      onState({ state: "failed", project: getProject(), detail: error.message });
      throw error;
    } finally {
      operation = null;
    }
  }

  async function restore(revision, run = false) {
    if (operation) throw new Error("A project operation is already running");
    if (!revision) throw new Error("No revision selected");
    const project = getProject();
    if (!project) throw new Error("No project selected");
    const transport = saveProtocol();
    const activation = run ? operationToken("activation") : "";
    operation = (async () => {
      onState({ state: "restoring", project, revision });
      await transport.request("revision.restore", { project, revision });
      if (!run) {
        const restored = { state: "restored", project, revision };
        onState(restored);
        return restored;
      }
      onState({ state: "activating", project, revision });
      const requested = await transport.request("project.activate", { project, activation });
      if (requested.activation !== activation)
        throw new Error("Device did not acknowledge the activation token");
      return waitForActivation(transport, activation, { project, onState });
    })();
    try {
      return await operation;
    } catch (error) {
      onState({ state: "failed", project, revision, detail: error.message });
      throw error;
    } finally {
      operation = null;
    }
  }

  return {
    save,
    restore,
    get busy() { return operation !== null; }
  };
}
