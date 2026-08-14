import assert from "node:assert/strict";
import test from "node:test";
import vm from "node:vm";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const runtime = readFileSync(resolve(here, "../../engine/runtime/retained.js"), "utf8");

function networkContext() {
  const callbacks = new Map();
  const sends = [];
  const tiles = [];
  let nextHandle = 1;
  const fx = {
    width: 1920,
    height: 1080,
    _netFetch: async url => ({ status: 200, url, body: '{"answer":42}',
      bodyBytes: Uint8Array.from([1, 2, 3]).buffer }),
    _netUdpOpen: () => nextHandle++,
    _netTcpConnect: () => nextHandle++,
    _netTcpListen: () => nextHandle++,
    _netOn(handle, event, callback) { callbacks.set(`${handle}:${event}`, callback); },
    _netSend(handle, data, host, port) {
      sends.push({ handle, data: new Uint8Array(data), host, port });
      return data.byteLength;
    },
    _netClose() {},
    _tileMapCreate: () => 0,
    _tileMapBegin(handle, generation, count) { tiles.push(["begin", handle, generation, count]); },
    _tileMapTile(handle, generation, index, x, y, size, bytes) {
      tiles.push(["tile", handle, generation, index, x, y, size, bytes.byteLength]);
    },
    _tileMapVisible() {},
    _tileMapReady: () => true,
    _cacheRead: () => Uint8Array.from([1, 2, 3]).buffer,
    _cacheWrite: () => true
  };
  const context = vm.createContext({ fx, console });
  vm.runInContext(runtime, context, { filename: "retained.js" });
  return { context, callbacks, sends, tiles };
}

test("fetch exposes a standard response surface", async () => {
  const { context } = networkContext();
  const result = await vm.runInContext(`
    fetch("https://example.test/data").then(async response => ({
      ok: response.ok,
      status: response.status,
      body: await response.json(),
      bytes: Array.from(new Uint8Array(await response.arrayBuffer()))
    }))
  `, context);
  assert.equal(result.ok, true);
  assert.equal(result.status, 200);
  assert.equal(result.body.answer, 42);
  assert.deepEqual(Array.from(result.bytes), [1, 2, 3]);
});

test("UDP and TCP wrappers carry bytes and peer metadata", () => {
  const { context, callbacks, sends } = networkContext();
  vm.runInContext(`
    globalThis.received = "";
    globalThis.peerPort = 0;
    const udp = fx.net.udp.open({ port: 9000 });
    udp.onMessage((data, peer) => {
      received = fx.net.decode(data);
      peerPort = peer.port;
    });
    udp.send("hello", "192.0.2.1", 9001);
  `, context);
  callbacks.get("1:1")(Uint8Array.from([111, 107]).buffer,
                        { address: "192.0.2.2", port: 5000 });
  assert.equal(context.received, "ok");
  assert.equal(context.peerPort, 5000);
  assert.equal(Buffer.from(sends[0].data).toString(), "hello");
  assert.equal(sends[0].port, 9001);
});

test("tile maps project coordinates and submit one atomic cached generation", async () => {
  const { context, tiles } = networkContext();
  const result = await vm.runInContext(`
    (() => {
      const map = fx.tileMap({
        source: { url: "https://tiles.test/{z}/{x}/{y}.png", tileSize: 256 },
        center: [12.635, 55.67], zoom: 11.45, cacheDays: 7
      });
      const center = map.project(12.635, 55.67);
      return map.ready().then(() => ({ center, ready: map.isReady() }));
    })()
  `, context);
  assert.ok(Math.abs(result.center.x - 960) < 0.001);
  assert.ok(Math.abs(result.center.y - 540) < 0.001);
  assert.equal(result.ready, true);
  assert.equal(tiles[0][0], "begin");
  assert.equal(tiles.filter(value => value[0] === "tile").length, tiles[0][3]);
});

test("HTTP server parsing and response generation stay in JavaScript", async () => {
  const { context, callbacks, sends } = networkContext();
  vm.runInContext(`
    fx.net.http.serve({ port: 8080 }, request => ({
      status: 200,
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ method: request.method, path: request.path })
    }));
  `, context);
  callbacks.get("1:4")(2);
  const request = Buffer.from("GET /status HTTP/1.1\r\nHost: device\r\n\r\n");
  callbacks.get("2:1")(request.buffer.slice(request.byteOffset,
                                             request.byteOffset + request.byteLength));
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  const response = Buffer.from(sends[0].data).toString();
  assert.match(response, /^HTTP\/1\.1 200 OK\r\n/);
  assert.match(response, /\r\n\r\n\{"method":"GET","path":"\/status"\}$/);
});
