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
  const passes = [];
  const requests = [];
  const cacheReads = [];
  const tileVisibility = [];
  const textureVisibility = [];
  let nextHandle = 1;
  const fx = {
    width: 1920,
    height: 1080,
    data: () => null,
    _netFetch: async (url, headers) => {
      requests.push({ url, headers });
      return { status: 200, url, body: '{"answer":42}',
        bodyBytes: Uint8Array.from([1, 2, 3]).buffer };
    },
    _netUdpOpen: () => nextHandle++,
    _netTcpConnect: () => nextHandle++,
    _netTcpListen: () => nextHandle++,
    _netWebSocketConnect: () => nextHandle++,
    _netWebSocketSend(handle, data) {
      sends.push({ handle, data: new Uint8Array(data) });
      return data.byteLength;
    },
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
    _tileMapVisible(handle, visible) { tileVisibility.push([handle, visible]); },
    _tileMapReady: () => true,
    _gpuTextureMap(map) { passes.push(["texture", map]);return 0x07000000; },
    _gpuTextureAsset(path) { passes.push(["textureAsset", path]);return 0x07000000; },
    _gpuTextureSecondaryMap(handle, map) {
      passes.push(["secondaryMap", handle, map]);
    },
    _gpuTextureSecondaryAsset(handle, path) {
      passes.push(["secondaryAsset", handle, path]);
    },
    _gpuTextureShader(handle, path) { passes.push(["shader", handle, path]); },
    _gpuTextureParams(handle, values) { passes.push(["params", handle, values.slice()]); },
    _gpuTextureField(handle, width, height, bytes) {
      passes.push(["field", handle, width, height, bytes.byteLength]);
    },
    _gpuTextureStage(handle, stage) { passes.push(["stage", handle, stage]); },
    _gpuTextureBlend(handle, blend) { passes.push(["blend", handle, blend]); },
    _gpuTextureOpacity(handle, opacity) { passes.push(["textureOpacity", handle, opacity]); },
    _gpuTextureVisible(handle, visible) { textureVisibility.push([handle, visible]); },
    _cacheRead(namespace, key) {
      cacheReads.push([namespace, key]);return Uint8Array.from([1, 2, 3]).buffer;
    },
    _cacheWrite: () => true,
    _hdf5Decode(bytes, dataset, start, count, stride, attributes) {
      return {
        format: "hdf5",
        dataset,
        type: "uint8",
        shape: [2, 2],
        sourceShape: [4, 4],
        buffer: Uint8Array.from([1, 2, 3, 4]).buffer,
        attributes: { "/what": { gain: 0.5, offset: -32 } },
        request: { size: bytes.byteLength, start, count, stride, attributes }
      };
    }
  };
  const context = vm.createContext({ fx, console });
  vm.runInContext(runtime, context, { filename: "retained.js" });
  return { context, callbacks, sends, tiles, passes, requests, cacheReads,
    tileVisibility, textureVisibility };
}

test("fetch exposes a standard response surface", async () => {
  const { context, requests } = networkContext();
  const result = await vm.runInContext(`
    fetch("https://example.test/data", {
      headers: { "User-Agent": "microFX-test/1 (+https://example.test)" }
    }).then(async response => ({
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
  assert.equal(requests[0].headers,
    "User-Agent: microFX-test/1 (+https://example.test)");
});

test("generic data decoder exposes typed HDF5 datasets and selections", () => {
  const { context } = networkContext();
  const result = vm.runInContext(`(() => {
    const decoded = fx.data.decode(new Uint8Array([1, 2, 3]), {
      format: "hdf5",
      dataset: "/dataset1/data1/data",
      start: [4, 8],
      count: [2, 2],
      stride: [3, 3],
      attributes: ["/what"]
    });
    return {
      dataset: decoded.dataset,
      values: Array.from(decoded.data),
      gain: decoded.attributes["/what"].gain,
      request: decoded.request
    };
  })()`, context);
  assert.equal(result.dataset, "/dataset1/data1/data");
  assert.deepEqual(Array.from(result.values), [1, 2, 3, 4]);
  assert.equal(result.gain, 0.5);
  assert.deepEqual(Array.from(result.request.start), [4, 8]);
  assert.deepEqual(Array.from(result.request.count), [2, 2]);
  assert.deepEqual(Array.from(result.request.stride), [3, 3]);
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

test("WebSocket wrapper exposes text messages and lifecycle callbacks", () => {
  const { context, callbacks, sends } = networkContext();
  vm.runInContext(`
    globalThis.opened = false;
    globalThis.message = "";
    const socket = fx.net.websocket.connect("wss://stream.example.test/feed");
    socket.onOpen(() => { opened = true; socket.send("subscribe"); });
    socket.onMessage(value => { message = value; });
  `, context);
  callbacks.get("1:0")();
  callbacks.get("1:1")(Uint8Array.from([115, 104, 105, 112]).buffer);
  assert.equal(context.opened, true);
  assert.equal(context.message, "ship");
  assert.equal(Buffer.from(sends[0].data).toString(), "subscribe");
});

test("tile maps project coordinates and submit one atomic cached generation", async () => {
  const { context, tiles, passes } = networkContext();
  const result = await vm.runInContext(`
    (() => {
      const map = fx.tileMap({
        source: { url: "https://tiles.test/{z}/{x}/{y}.png", tileSize: 256 },
        center: [12.635, 55.67], zoom: 11.45, cacheDays: 7
      });
      const center = map.project(12.635, 55.67);
      const location = map.unproject(center.x, center.y);
      fx.texture(map)
         .shader("assets/shaders/weather.fs")
         .field(2, 1, new Uint8Array(8))
         .params([1, 2, 3])
         .stage("overlay")
         .blend(true);
      return map.ready().then(() => {
        map.viewport(-73.93, 40.715, 10.5);
        return map.ready();
      }).then(() => ({
        center,
        location,
        moved: map.project(-73.93, 40.715),
        ready: map.isReady()
      }));
    })()
  `, context);
  assert.ok(Math.abs(result.center.x - 960) < 0.001);
  assert.ok(Math.abs(result.center.y - 540) < 0.001);
  assert.ok(Math.abs(result.location.longitude - 12.635) < 0.000001);
  assert.ok(Math.abs(result.location.latitude - 55.67) < 0.000001);
  assert.ok(Math.abs(result.moved.x - 960) < 0.001);
  assert.ok(Math.abs(result.moved.y - 540) < 0.001);
  assert.equal(result.ready, true);
  assert.equal(tiles[0][0], "begin");
  const generations = tiles.filter(value => value[0] === "begin");
  assert.equal(generations.length, 2,
    "viewport must start exactly one additional tile generation");
  assert.equal(tiles.filter(value => value[0] === "tile").length,
    generations[0][3] + generations[1][3]);
  assert.deepEqual(passes.map(value => value[0]),
    ["texture", "shader", "field", "params", "stage", "blend"]);
  assert.deepEqual(Array.from(passes[3][2]), [1, 2, 3]);
});

test("scene selection gates tile maps without rewriting their visibility", () => {
  const { context, tiles, tileVisibility, textureVisibility } = networkContext();
  vm.runInContext(`
    const map = fx.tileMap({
      source: { url: "https://tiles.test/{z}/{x}/{y}.png", tileSize: 256 },
      center: [12.635, 55.67], zoom: 11, cacheDays: 7
    });
    const disabled = fx.tileMap({
      enabled: false,
      source: { url: "https://disabled.test/{z}/{x}/{y}.png", tileSize: 256 },
      center: [12.635, 55.67], zoom: 11, cacheDays: 7
    });
    const texture = fx.texture(map, { enabled: false });
    const scene = fx.scenes.add(fx.scene({ name: "map-gate" }));
    scene.add(map);
    map.hide();
    fx._beginFrame();scene.show();fx._endFrame();
    texture.enabled(true).hide();
  `, context);
  assert.equal(tiles.filter(call => call[0] === "begin").length, 1,
    "a disabled tile map must not start loading");
  assert.equal(tileVisibility.at(-1)[1], false,
    "scene.show must preserve the map's own hidden state");
  assert.deepEqual(textureVisibility.slice(-3).map(call => call[1]),
    [false, true, false]);
});

test("solar position is geographic and world-wide", () => {
  const { context } = networkContext();
  const result = vm.runInContext(`(() => {
    const sunrise = fx.geo.sunPosition(new Date("2026-03-20T06:00:00Z"), 0, 0);
    const noon = fx.geo.sunPosition(new Date("2026-03-20T12:00:00Z"), 0, 0);
    const sunset = fx.geo.sunPosition(new Date("2026-03-20T18:00:00Z"), 0, 0);
    const arcticWinter = fx.geo.sunPosition(
      new Date("2026-12-21T11:00:00Z"), 69.6492, 18.9553);
    const southernSummer = fx.geo.sunPosition(
      new Date("2026-12-21T02:00:00Z"), -33.8688, 151.2093);
    return { sunrise, noon, sunset, arcticWinter, southernSummer };
  })()`, context);
  assert.ok(result.sunrise.east > 0.9);
  assert.ok(result.noon.sinElevation > 0.99);
  assert.ok(result.sunset.east < -0.9);
  assert.equal(result.arcticWinter.daylight, false);
  assert.equal(result.southernSummer.daylight, true);
});

test("oblique stereographic projection round-trips DMI radar coordinates", () => {
  const { context } = networkContext();
  const result = vm.runInContext(`(() => {
    const projection = fx.geo.obliqueStereographic({
      latitudeOrigin: 56,
      longitudeOrigin: 10.5666
    });
    const upperLeft = projection.forward(3, 60);
    const copenhagen = projection.inverse(125919.510827, -34246.066198);
    const projected = projection.forward(12.5683, 55.6761);
    const roundTrip = projection.inverse(projected.x, projected.y);
    return { upperLeft, copenhagen, roundTrip };
  })()`, context);
  assert.ok(Math.abs(result.upperLeft.x - -422114.801161) < 0.01);
  assert.ok(Math.abs(result.upperLeft.y - 469381.012754) < 0.01);
  assert.ok(Math.abs(result.copenhagen.longitude - 12.5683) < 0.01);
  assert.ok(Math.abs(result.copenhagen.latitude - 55.6761) < 0.01);
  assert.ok(Math.abs(result.roundTrip.longitude - 12.5683) < 1e-8);
  assert.ok(Math.abs(result.roundTrip.latitude - 55.6761) < 1e-8);
});

test("earth maps align global day and fixed-date night imagery", async () => {
  const { context, passes, cacheReads } = networkContext();
  const result = await vm.runInContext(`
    (() => {
      const earth = fx.maps.earth({ center: [12.5683, 55.6761], zoom: 11.25 });
      fx.texture(earth.day)
        .secondary(earth.night)
        .shader("assets/shaders/day-night.fs")
        .params([0.5]);
      earth.hide();
      return earth.ready().then(() => ({
        date: earth.nightDate,
        center: earth.project(12.5683, 55.6761),
        ready: earth.isReady()
      }));
    })()
  `, context);
  assert.equal(result.date, "2016-01-01");
  assert.ok(Math.abs(result.center.x - 960) < 0.001);
  assert.ok(Math.abs(result.center.y - 540) < 0.001);
  assert.equal(result.ready, true);
  const nightUrls = cacheReads.map(value => value[1])
    .filter(value => value.includes("gibs.earthdata.nasa.gov"));
  assert.ok(nightUrls.length > 0);
  assert.ok(nightUrls.every(value =>
    value.includes("/2016-01-01/GoogleMapsCompatible_Level8/8/")));
  assert.deepEqual(passes.map(value => value[0]),
    ["texture", "secondaryMap", "shader", "params"]);
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
