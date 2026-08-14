import assert from "node:assert/strict";
import test from "node:test";
import { recentPeerIds, rememberPeerId, uploadAssetBatch } from "../studio-state.js";

test("recent peer IDs tolerate malformed storage and retain the legacy fallback", () => {
  assert.deepEqual(recentPeerIds("not-json", "microfx-demo"), ["microfx-demo"]);
  assert.deepEqual(recentPeerIds('["one","two","one"]', "two"), ["one", "two"]);
});

test("remembered peers are most-recent-first, unique, and bounded", () => {
  assert.deepEqual(rememberPeerId('["b","c","d"]', "c", 3), ["c", "b", "d"]);
});

test("asset batches continue after one independent upload fails", async () => {
  const attempted = [];
  const files = [{ name: "one" }, { name: "bad" }, { name: "three" }];
  const result = await uploadAssetBatch(files, async (file) => {
    attempted.push(file.name);
    if (file.name === "bad") throw new Error("rejected");
  });
  assert.deepEqual(attempted, ["one", "bad", "three"]);
  assert.deepEqual(result.uploaded.map(({ file }) => file.name), ["one", "three"]);
  assert.deepEqual(result.failed.map(({ file }) => file.name), ["bad"]);
});
