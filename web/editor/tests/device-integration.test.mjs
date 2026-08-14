import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { chmod, lstat, mkdtemp, mkdir, readFile, readdir, readlink, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";
import test from "node:test";
import { createStudioActions } from "../actions.js";
import { DeviceProtocol, saveProject, uploadAsset } from "../protocol.js";
import { ReconnectSession } from "../reconnect-session.js";

const cli = process.env.MICROFX_PROTOCOL_CLI;
const supervisor = process.env.MICROFX_SUPERVISOR;

function waitForExit(child, timeoutMs = 3000) {
  if (child.exitCode !== null || child.signalCode !== null) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("process did not exit")), timeoutMs);
    const done = () => { clearTimeout(timer); resolve(); };
    child.once("exit", done);
    // Avoid losing an exit that races between the initial check and listener.
    if (child.exitCode !== null || child.signalCode !== null) done();
  });
}

async function stop(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  child.kill("SIGTERM");
  try {
    await waitForExit(child);
  } catch {
    child.kill("SIGKILL");
    await waitForExit(child);
  }
}

function connectProtocol(child, timeoutMs = 5000) {
  const lines = createInterface({ input: child.stdout });
  let sequence = 0;
  const protocol = new DeviceProtocol((raw) => child.stdin.write(`${raw}\n`), {
    timeoutMs, createId: () => `integration-${++sequence}`
  });
  lines.on("line", (line) => protocol.receive(JSON.parse(line)));
  return { protocol, lines };
}

test("serialized Studio workflow creates, saves, activates, retrieves, and restores", {
  skip: !cli && "run through services/peer-bridge/tests/run.sh"
}, async () => {
  const sandbox = await mkdtemp(join(tmpdir(), "microfx-device-integration."));
  await mkdir(join(sandbox, "apps", "projects"), { recursive: true });
  await mkdir(join(sandbox, "run"), { recursive: true });
  const child = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
  const { protocol, lines } = connectProtocol(child, 1000);

  try {
    await protocol.request("project.create", {
      name: "studio-test", metadata: { title: "Studio integration test" }
    });
    const imageCode = "const pixel=fx.image(\"assets/images/pixel.png\",10,20,32,32);";
    const operation = saveProject(protocol, "studio-test", imageCode, true, {
      pollMs: 5, timeoutMs: 1000
    });
    while (true) {
      try {
        const request = await readFile(join(sandbox, "run", "reload"), "utf8");
        const [activation, project] = request.trim().split("\t");
        await writeFile(join(sandbox, "run", "status"),
          `${activation}\t${project}\trunning\trenderer passed health check\n`);
        break;
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
    }
    const running = await operation;
    assert.equal(running.state, "running");
    assert.equal(running.project, "studio-test");
    assert.equal(await readFile(join(sandbox, "apps", "projects", "studio-test", "main.js"), "utf8"),
      imageCode);

    await protocol.request("asset.put", {
      project: "studio-test", path: "images/pixel.png",
      content: "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
    });
    let project = await protocol.request("project.get", { project: "studio-test" });
    assert.equal(project.code, imageCode);
    assert.deepEqual(project.assets, [{ path: "images/pixel.png", size: 68 }]);
    const image = await protocol.request("asset.get", {
      project: "studio-test", path: "images/pixel.png"
    });
    assert.equal(image.encoding, "base64");
    assert.equal(image.content,
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");

    await protocol.request("code.put", { project: "studio-test", content: "fx.rect(1,2,3,4);" });
    project = await protocol.request("project.get", { project: "studio-test" });
    assert.ok(project.revisions.length >= 2);
    const previous = project.revisions.at(-1);
    const snapshot = await protocol.request("revision.get", {
      project: "studio-test", revision: previous
    });
    assert.equal(snapshot.code, imageCode);
    assert.equal(snapshot.metadata.title, "Studio integration test");
    assert.deepEqual(snapshot.assets, [{ path: "images/pixel.png", size: 68 }]);
    await protocol.request("project.metadata.put", {
      project: "studio-test", metadata: { title: "Temporary title" }
    });
    await protocol.request("asset.delete", {
      project: "studio-test", path: "images/pixel.png"
    });
    await protocol.request("revision.restore", { project: "studio-test", revision: previous });
    project = await protocol.request("project.get", { project: "studio-test" });
    assert.equal(project.code, imageCode);
    assert.equal(project.metadata.title, "Studio integration test");
    assert.deepEqual(project.assets, [{ path: "images/pixel.png", size: 68 }]);
    const restoredImage = await protocol.request("asset.get", {
      project: "studio-test", path: "images/pixel.png"
    });
    assert.equal(restoredImage.content,
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");

    await protocol.request("asset.delete", { project: "studio-test", path: "images/pixel.png" });
    await assert.rejects(
      protocol.request("asset.get", { project: "studio-test", path: "images/pixel.png" }),
      /could not read asset/
    );
  } finally {
    protocol.close();
    child.stdin.end();
    lines.close();
    child.kill();
    await rm(sandbox, { recursive: true, force: true });
  }
});

test("large asset upload resumes after bridge interruption", {
  skip: !cli && "run through services/peer-bridge/tests/run.sh"
}, async () => {
  const sandbox = await mkdtemp(join(tmpdir(), "microfx-resume-integration."));
  await mkdir(join(sandbox, "apps", "projects"), { recursive: true });
  const bytes = new Uint8Array(320000);
  for (let index = 0; index < bytes.length; index++) bytes[index] = (index * 17) % 251;
  let firstChild;
  let secondChild;
  let firstLines;
  let secondLines;
  try {
    firstChild = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
    const first = connectProtocol(firstChild, 3000);
    firstLines = first.lines;
    const session = new ReconnectSession({ timeoutMs: 5000 });
    session.attach(first.protocol);
    await first.protocol.request("project.create", {
      name: "resume-test", metadata: { title: "Resume test" }
    });
    const request = first.protocol.request.bind(first.protocol);
    let interrupted = false;
    let markInterrupted;
    const firstChunk = new Promise((resolve) => { markInterrupted = resolve; });
    first.protocol.request = async (type, fields) => {
      const response = await request(type, fields);
      if (type === "asset.upload.chunk" && !interrupted) {
        interrupted = true;
        session.detach(first.protocol);
        markInterrupted();
        throw new Error("simulated transport interruption");
      }
      return response;
    };
    const progress = [];
    const operation = session.retry((protocol) =>
      uploadAsset(protocol, "resume-test", "large.bin", bytes,
        { onProgress: ({ offset }) => progress.push(offset) }));
    await firstChunk;
    first.protocol.close();
    firstChild.stdin.end();
    firstLines.close();
    await stop(firstChild);

    secondChild = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
    const second = connectProtocol(secondChild, 3000);
    secondLines = second.lines;
    session.attach(second.protocol);
    await operation;
    assert.ok(progress.length > 0);
    assert.ok(progress[0] > 48 * 1024, "the resumed upload must retain its first chunk");
    assert.deepEqual(
      new Uint8Array(await readFile(join(sandbox, "apps", "projects", "resume-test", "assets", "large.bin"))),
      bytes
    );
    second.protocol.close();
    secondChild.stdin.end();
    secondLines.close();
    await stop(secondChild);
  } finally {
    await stop(firstChild);
    await stop(secondChild);
    firstLines?.close();
    secondLines?.close();
    await rm(sandbox, { recursive: true, force: true });
  }
});

test("Save & Run replays its transaction after a lost acknowledgement without restarting", {
  skip: !cli && "run through services/peer-bridge/tests/run.sh",
  timeout: 10000
}, async () => {
  const sandbox = await mkdtemp(join(tmpdir(), "microfx-save-resume."));
  const apps = join(sandbox, "apps");
  const run = join(sandbox, "run");
  const reload = join(run, "reload");
  const status = join(run, "status");
  await mkdir(join(apps, "projects"), { recursive: true });
  await mkdir(run, { recursive: true });
  let firstChild;
  let secondChild;
  let firstLines;
  let secondLines;
  try {
    firstChild = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
    const first = connectProtocol(firstChild, 3000);
    firstLines = first.lines;
    const session = new ReconnectSession({ timeoutMs: 5000 });
    session.attach(first.protocol);
    await first.protocol.request("project.create", {
      name: "save-resume", metadata: { title: "Save resume" }
    });

    const originalRequest = first.protocol.request.bind(first.protocol);
    let activationProcessed;
    const processed = new Promise((resolve) => { activationProcessed = resolve; });
    first.protocol.request = async (type, fields) => {
      const response = await originalRequest(type, fields);
      if (type === "project.save-run") {
        session.detach(first.protocol);
        activationProcessed();
        throw new Error("simulated lost Save & Run acknowledgement");
      }
      return response;
    };

    const activation = "stable-save-resume";
    const operation = saveProject(session, "save-resume", "fx.rect(1,2,3,4);", true, {
      activation, pollMs: 5, timeoutMs: 5000
    });
    await processed;
    assert.equal((await readFile(reload, "utf8")).trim(), `${activation}\tsave-resume`);
    await writeFile(status,
      `${activation}\tsave-resume\trunning\trenderer passed health check\n`);
    await rm(reload, { force: true });
    first.protocol.close();
    firstChild.stdin.end();
    firstLines.close();
    await stop(firstChild);

    secondChild = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
    const second = connectProtocol(secondChild, 3000);
    secondLines = second.lines;
    session.attach(second.protocol);
    const running = await operation;
    assert.equal(running.state, "running");
    assert.equal(running.project, "save-resume");
    await assert.rejects(lstat(reload), { code: "ENOENT" });
    assert.equal(await readFile(join(apps, "projects", "save-resume", "main.js"), "utf8"),
      "fx.rect(1,2,3,4);");
    second.protocol.close();
    secondChild.stdin.end();
    secondLines.close();
    await stop(secondChild);
  } finally {
    await stop(firstChild);
    await stop(secondChild);
    firstLines?.close();
    secondLines?.close();
    await rm(sandbox, { recursive: true, force: true });
  }
});

test("Restore & Run resumes after a lost restore acknowledgement without duplicate revisions", {
  skip: !cli && "run through services/peer-bridge/tests/run.sh",
  timeout: 10000
}, async () => {
  const sandbox = await mkdtemp(join(tmpdir(), "microfx-restore-resume."));
  const apps = join(sandbox, "apps");
  const projectRoot = join(apps, "projects", "restore-resume");
  const run = join(sandbox, "run");
  const reload = join(run, "reload");
  const status = join(run, "status");
  await mkdir(join(apps, "projects"), { recursive: true });
  await mkdir(run, { recursive: true });
  let firstChild;
  let secondChild;
  let firstLines;
  let secondLines;
  try {
    firstChild = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
    const first = connectProtocol(firstChild, 3000);
    firstLines = first.lines;
    const session = new ReconnectSession({ timeoutMs: 5000 });
    session.attach(first.protocol);
    await first.protocol.request("project.create", {
      name: "restore-resume", operation: "project-create-restore-resume",
      metadata: { title: "Restore resume" }
    });
    await first.protocol.request("code.put", {
      project: "restore-resume", content: "fx.rect(1,2,3,4);"
    });
    await first.protocol.request("code.put", {
      project: "restore-resume", content: "fx.circle(20,20,10);"
    });
    const before = await first.protocol.request("project.get", { project: "restore-resume" });
    const revision = before.revisions.at(-1);
    assert.ok(revision);

    const originalRequest = first.protocol.request.bind(first.protocol);
    let restoreProcessed;
    const processed = new Promise((resolve) => { restoreProcessed = resolve; });
    first.protocol.request = async (type, fields) => {
      const response = await originalRequest(type, fields);
      if (type === "revision.restore") {
        session.detach(first.protocol);
        restoreProcessed();
        throw new Error("simulated lost restore acknowledgement");
      }
      return response;
    };
    const actions = createStudioActions({
      protocol: () => session.protocol,
      saveProtocol: () => session,
      getProject: () => "restore-resume",
      getCode: () => ""
    });
    const operation = actions.restore(revision, true);
    await processed;
    assert.equal(await readFile(join(projectRoot, "main.js"), "utf8"), "fx.rect(1,2,3,4);");
    const revisionsAfterFirstRestore = (await readdir(join(projectRoot, "revisions")))
      .filter((name) => /^r\d+$/.test(name)).length;

    first.protocol.close();
    firstChild.stdin.end();
    firstLines.close();
    await stop(firstChild);

    secondChild = spawn(cli, [sandbox], { stdio: ["pipe", "pipe", "inherit"] });
    const second = connectProtocol(secondChild, 3000);
    secondLines = second.lines;
    session.attach(second.protocol);
    while (true) {
      try {
        const request = await readFile(reload, "utf8");
        const [activation, project] = request.trim().split("\t");
        await writeFile(status,
          `${activation}\t${project}\trunning\trenderer passed health check\n`);
        break;
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
    }
    const running = await operation;
    assert.equal(running.state, "running");
    assert.equal(running.project, "restore-resume");
    const revisionsAfterReplay = (await readdir(join(projectRoot, "revisions")))
      .filter((name) => /^r\d+$/.test(name)).length;
    assert.equal(revisionsAfterReplay, revisionsAfterFirstRestore);
    second.protocol.close();
    secondChild.stdin.end();
    secondLines.close();
    await stop(secondChild);
  } finally {
    await stop(firstChild);
    await stop(secondChild);
    firstLines?.close();
    secondLines?.close();
    await rm(sandbox, { recursive: true, force: true });
  }
});

test("real Studio action, handler, and supervisor complete Save & Run and recover", {
  skip: (!cli || !supervisor) && "run through services/peer-bridge/tests/run.sh",
  timeout: 20000
}, async () => {
  const sandbox = await mkdtemp(join(tmpdir(), "microfx-save-run-e2e."));
  const data = join(sandbox, "data");
  const run = join(sandbox, "run");
  const state = join(data, "state");
  const renderer = join(sandbox, "renderer");
  await mkdir(join(data, "apps", "projects"), { recursive: true });
  await mkdir(join(data, "config"), { recursive: true });
  await mkdir(state, { recursive: true });
  await mkdir(run, { recursive: true });
  await writeFile(renderer, `#!/bin/sh
set -eu
code=$(cat "$MICROFX_DATA_ROOT/apps/current/main.js")
case "$code" in
  *FAIL_RENDERER*) echo "script compile failed: FAIL_RENDERER" >&2; exit 23 ;;
esac
printf 'start:%s\n' "$code" >>"$MICROFX_DATA_ROOT/state/renderer-starts.log"
trap 'exit 0' TERM INT
while :; do sleep 1; done
`);
  await chmod(renderer, 0o755);

  const protocolProcess = spawn(cli, [sandbox], {
    stdio: ["pipe", "pipe", "inherit"],
    env: {
      ...process.env,
      MICROFX_TEST_APPS_ROOT: join(data, "apps"),
      MICROFX_TEST_RELOAD_SIGNAL: join(run, "microfx-project-reload"),
      MICROFX_TEST_RELOAD_STATUS: join(run, "microfx-project-status"),
      MICROFX_TEST_CONSOLE_LOG: join(sandbox, "canvas.log")
    }
  });
  const { protocol, lines } = connectProtocol(protocolProcess);
  const supervisorProcess = spawn(supervisor, [], {
    stdio: ["ignore", "ignore", "inherit"],
    env: {
      ...process.env,
      CANVAS_CONFIG: join(sandbox, "missing-canvas.conf"),
      MICROFX_PRODUCT_CONFIG: join(sandbox, "missing-product.conf"),
      MICROFX_DATA_ROOT: data,
      MICROFX_RUN_ROOT: run,
      MICROFX_FACTORY_APP: renderer,
      MICROFX_ONBOARDING_SCRIPT: join(sandbox, "missing-onboarding.js"),
      MICROFX_REQUIRE_DATA_MOUNT: "0",
      MICROFX_HEALTH_SECONDS: "0.2",
      CANVAS_FAIL_FAST: "1"
    }
  });

  const states = [];
  let editorCode = "";
  const actions = createStudioActions({
    protocol: () => protocol,
    saveProtocol: () => protocol,
    getProject: () => "live-test",
    getCode: () => editorCode,
    onState: (value) => states.push(value)
  });
  try {
    await protocol.request("project.create", {
      name: "live-test", metadata: { title: "Live Save and Run" }
    });
    editorCode = "fx.circle(20,30,8);";
    const firstOperation = actions.save(true);
    assert.equal(actions.busy, true);
    await assert.rejects(actions.save(true), /already running/);
    const first = await firstOperation;
    assert.equal(actions.busy, false);
    assert.equal(first.state, "running");
    assert.equal(first.project, "live-test");
    assert.deepEqual(states.slice(0, 2).map((value) => value.state),
      ["saving", "activating"]);
    assert.ok(states.some((value) => value.state === "stopping" || value.state === "pending"));
    assert.equal(states.at(-1).state, "running");
    assert.equal(await readlink(join(data, "apps", "current")),
      join(data, "apps", "projects", "live-test"));
    assert.match(await readFile(join(state, "renderer-starts.log"), "utf8"),
      /fx\.circle\(20,30,8\);/);

    editorCode = "FAIL_RENDERER";
    await assert.rejects(actions.save(true), /script compile failed: FAIL_RENDERER/);
    assert.equal(states.at(-1).state, "failed");
    assert.match(states.at(-1).detail, /script compile failed: FAIL_RENDERER/);
    assert.equal(actions.busy, false);

    editorCode = "fx.rect(1,2,3,4);";
    const recovered = await actions.save(true);
    assert.equal(recovered.state, "running");
    const starts = await readFile(join(state, "renderer-starts.log"), "utf8");
    assert.match(starts, /fx\.rect\(1,2,3,4\);/);
    const project = await protocol.request("project.get", { project: "live-test" });
    assert.equal(project.code, "fx.rect(1,2,3,4);");
    assert.ok(project.revisions.length >= 3);
    assert.equal((await lstat(join(data, "apps", "current"))).isSymbolicLink(), true);
  } finally {
    protocol.close();
    protocolProcess.stdin.end();
    lines.close();
    await stop(protocolProcess);
    await stop(supervisorProcess);
    await rm(sandbox, { recursive: true, force: true });
  }
});
