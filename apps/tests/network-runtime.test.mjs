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
  let nextHandle = 1;
  const fx = {
    _netFetch: async url => ({ status: 200, url, body: '{"answer":42}' }),
    _netUdpOpen: () => nextHandle++,
    _netTcpConnect: () => nextHandle++,
    _netTcpListen: () => nextHandle++,
    _netOn(handle, event, callback) { callbacks.set(`${handle}:${event}`, callback); },
    _netSend(handle, data, host, port) {
      sends.push({ handle, data: new Uint8Array(data), host, port });
      return data.byteLength;
    },
    _netClose() {}
  };
  const context = vm.createContext({ fx, console });
  vm.runInContext(runtime, context, { filename: "retained.js" });
  return { context, callbacks, sends };
}

test("fetch exposes a standard response surface", async () => {
  const { context } = networkContext();
  const result = await vm.runInContext(`
    fetch("https://example.test/data").then(async response => ({
      ok: response.ok,
      status: response.status,
      body: await response.json()
    }))
  `, context);
  assert.equal(result.ok, true);
  assert.equal(result.status, 200);
  assert.equal(result.body.answer, 42);
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
