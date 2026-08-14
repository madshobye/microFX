import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createAppRuntimeTest } from "./lib/runtime-test.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const runtimeSource = readFileSync(resolve(here, "../../engine/runtime/retained.js"), "utf8");
const root = resolve(here, "../projects/minimal-clock");

function app(source, options = {}) {
  return createAppRuntimeTest({ root, source, runtimeSource, ...options });
}

test("reports retained construction and finite update mutations", () => {
  const runtime = app(`
    const item = fx.rect(10, 20, 30, 40, 0xffffffff);
    function update(time, delta) { item.position(time, delta).opacity(0.5); }
  `);
  const report = runtime.runFrames(120, 60);
  assert.equal(report.counts.quad, 1);
  assert.equal(report.handles, 1);
  assert.equal(report.frames, 120);
  assert.equal(report.maximumFrameCalls, 2);
});

test("rejects per-frame GPU object allocation", () => {
  const runtime = app(`function update() { fx.circle(1, 2, 3, 0xffffffff); }`);
  assert.throws(() => runtime.runFrame(0, 1 / 30), /must be retained/);
});

test("rejects non-finite transforms", () => {
  const runtime = app(`
    const item = fx.rect(0, 0, 1, 1, 0xffffffff);
    function update() { item.position(Number.NaN, 0); }
  `);
  assert.throws(() => runtime.runFrame(0, 1 / 30), /must be finite/);
});

test("enforces native capacities at construction", () => {
  assert.throws(() => app(`
    const a = fx.rect(0, 0, 1, 1, 0xffffffff);
    const b = fx.rect(1, 0, 1, 1, 0xffffffff);
    function update() {}
  `, { capacities: { sdf: 1, quad: 1, mesh: 1, text: 1, image: 1 } }),
  /quad capacity exceeded/);
});

test("confines fixture data to the project assets directory", () => {
  assert.throws(() => app(`
    const data = fx.data("../../project.json");
    function update() {}
  `), /asset path leaves project/);
});
