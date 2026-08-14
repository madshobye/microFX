import assert from "node:assert/strict";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, join, normalize, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const apps = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const roots = [join(apps, "demo")];
for (const entry of readdirSync(join(apps, "projects"), { withFileTypes: true })) {
  if (entry.isDirectory()) roots.push(join(apps, "projects", entry.name));
}

for (const root of roots) {
  const script = root.endsWith("demo") ? join(root, "scripts", "main.js") : join(root, "main.js");
  const source = readFileSync(script, "utf8");
  for (const match of source.matchAll(/fx\.(?:model|image|backgroundImage|shader)\(\s*["']([^"']+)["']/g)) {
    const requested = normalize(match[1]);
    assert.equal(requested.startsWith("assets/") && !requested.includes(".."), true,
      `${script}: unsafe asset path ${match[1]}`);
    const absolute = resolve(root, requested);
    assert.equal(relative(resolve(root, "assets"), absolute).startsWith(".."), false,
      `${script}: asset escapes project: ${match[1]}`);
    assert.equal(existsSync(absolute), true, `${script}: missing ${match[1]}`);
  }
}

console.log("bundled asset reference tests passed");
