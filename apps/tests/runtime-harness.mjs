import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { loadAppRuntimeTest } from "./lib/runtime-test.mjs";

const apps = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeSource = readFileSync(resolve(apps, "../engine/runtime/retained.js"), "utf8");
const stressFrames = Number.parseInt(process.env.MICROFX_APP_TEST_FRAMES || "18000", 10);
assert.ok(Number.isInteger(stressFrames) && stressFrames > 0,
          "MICROFX_APP_TEST_FRAMES must be a positive integer");

function runProject(root, script) {
  const test = loadAppRuntimeTest({ root, script, runtimeSource });
  const report = test.runFrames(stressFrames, 30);
  assert.ok(Object.values(report.counts).some(value => value > 0), `${script}: empty scene`);
  assert.equal(report.handles,
               Object.values(report.counts).reduce((sum, value) => sum + value, 0),
               `${script}: native handle accounting drift`);
  return report;
}

function summary(name, report) {
  const counts = Object.entries(report.counts).map(([kind, value]) => `${kind}=${value}`).join(" ");
  console.log(`${name}: ${counts} frames=${report.frames} calls/frame(avg/max)=` +
              `${report.averageFrameCalls.toFixed(1)}/${report.maximumFrameCalls}`);
}

const projects = readdirSync(join(apps, "projects"), { withFileTypes: true })
  .filter(entry => entry.isDirectory()).map(entry => entry.name).sort();
assert.equal(projects.length, 10, "expected ten selectable project folders");

for (const name of projects) {
  const root = join(apps, "projects", name);
  summary(name, runProject(root, join(root, "main.js")));
}

const fallbackRoot = join(apps, "demo");
summary("factory-demo", runProject(fallbackRoot, join(fallbackRoot, "scripts", "main.js")));
console.log(`bundled runtime stress harness passed (${stressFrames} frames per app)`);
