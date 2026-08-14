import assert from "node:assert/strict";
import vm from "node:vm";
import { isAbsolute, join, resolve, sep } from "node:path";
import { readFileSync } from "node:fs";

export const defaultCapacities = Object.freeze({
  sdf: 512,
  quad: 512,
  mesh: 256,
  text: 64,
  image: 16,
  outline: 384
});

function finite(label, values) {
  values.forEach((value, index) => {
    assert.equal(typeof value, "number", `${label}[${index}] must be numeric`);
    assert.ok(Number.isFinite(value), `${label}[${index}] must be finite`);
  });
}

function confinedAsset(root, path) {
  assert.equal(typeof path, "string", "asset path must be a string");
  assert.ok(path.length > 0, "asset path must not be empty");
  assert.ok(!isAbsolute(path), `asset path must be relative: ${path}`);
  const assetRoot = resolve(root, "assets");
  const target = resolve(assetRoot, path);
  assert.ok(target === assetRoot || target.startsWith(`${assetRoot}${sep}`),
            `asset path leaves project: ${path}`);
  return target;
}

function instrumentedNativeFx(root, capacities) {
  let nextHandle = 1;
  let phase = "construction";
  let frameCalls = 0;
  let maximumFrameCalls = 0;
  let totalUpdateCalls = 0;
  const handles = new Map();
  const counts = { sdf: 0, quad: 0, mesh: 0, text: 0, image: 0, outline: 0 };
  const mutations = {
    move: 0, transform: 0, text: 0, font: 0, textAntialias: 0, color: 0,
    sdfGeometry: 0, effect: 0, visible: 0, opacity: 0
  };

  function knownHandle(handle, label) {
    assert.equal(typeof handle, "number", `${label} handle must be numeric`);
    assert.ok(handles.has(handle), `${label} received unknown handle ${handle}`);
  }

  function mutation(label, handle, values) {
    knownHandle(handle, label);
    if (values) finite(label, values);
    mutations[label] += 1;
    if (phase === "update") frameCalls += 1;
    return true;
  }

  function add(kind) {
    return (...args) => {
      assert.equal(phase, "construction",
                   `${kind} GPU objects must be retained, not allocated by update()`);
      counts[kind] += 1;
      assert.ok(counts[kind] <= capacities[kind], `${kind} capacity exceeded`);
      const handle = nextHandle++;
      handles.set(handle, { kind, args });
      return handle;
    };
  }

  const fx = {
    width: 1920,
    height: 1080,
    _rect: add("quad"),
    _gradientRect: add("quad"),
    _background: add("quad"),
    _netFetch(url) {
      const body = url.includes("energidataservice")
        ? JSON.stringify({ records: [
            { TimeUTC: "2026-08-14T00:00:00", DayAheadPriceDKK: 320 },
            { TimeUTC: "2026-08-14T00:15:00", DayAheadPriceDKK: 280 }
          ] })
        : url.includes("opendata.adsb.fi")
        ? JSON.stringify({ now: 1786700000000, ac: [{
            hex: "abc", flight: "TEST123", lon: 12.5, lat: 55.7,
            alt_baro: 1000, gs: 90, track: 45, seen_pos: 0.2,
            category: "A1", t: "C172", desc: "CESSNA 172 Skyhawk"
          }] })
        : url.includes("api.transitous.org")
        ? JSON.stringify([{
            trips: [{ tripId: "test-train", displayName: "A" }],
            mode: "SUBURBAN", realTime: true,
            departure: new Date(Date.now() - 60000).toISOString(),
            arrival: new Date(Date.now() + 60000).toISOString(),
            from: { name: "Nørreport" }, to: { name: "Østerport" },
            polyline: "{qhaI_p`vAq@o@q@o@"
          }])
        : "tile";
      const bodyBytes = new TextEncoder().encode(body).buffer;
      return Promise.resolve({ status: 200, url, body, bodyBytes });
    },
    _tileMapCreate() { return 0; },
    _tileMapBegin(handle, generation, count) {
      assert.equal(handle, 0);assert.ok(generation > 0 && count > 0 && count <= 64);
    },
    _tileMapTile() {},
    _tileMapVisible() {},
    _tileMapReady() { return true; },
    _cacheRead() { return null; },
    _cacheWrite() { return true; },
    _netUdpOpen() { return 1; },
    _netTcpConnect() { return 1; },
    _netTcpListen() { return 1; },
    _netOn() {},
    _netSend() { return 0; },
    _netClose() {},
    _qrMatrix(value) {
      assert.equal(typeof value, "string");
      assert.ok(value.length > 0);
      return "111\n101\n111\n";
    },
    _circle: add("quad"),
    _sdfCircle: add("sdf"),
    _sdfRoundedRect: add("sdf"),
    _text: add("text"),
    _image: add("image"),
    _outline: add("outline"),
    _polygon: add("outline"),
    _backgroundImage: add("image"),
    _imageScale(handle, scale) {
      return mutation("transform", handle, [scale]);
    },
    _outlinePoints(handle, points) {
      knownHandle(handle, "outline points");
      assert.ok(Array.isArray(points));
      return mutation("transform", handle, points);
    },
    _outlineScale(handle, scale) {
      return mutation("transform", handle, [scale]);
    },
    _cube: add("mesh"),
    _sphere: add("mesh"),
    _wireCube: add("mesh"),
    _grid: add("mesh"),
    _model: add("mesh"),
    _move(handle, x, y, rotation) {
      return mutation("move", handle, [x, y, rotation]);
    },
    _transform(handle, x, y, z, rx, ry, rz, scale) {
      return mutation("transform", handle, [x, y, z, rx, ry, rz, scale]);
    },
    _setText(handle, value) {
      knownHandle(handle, "text");
      assert.ok(typeof value === "string" || typeof value === "number",
                "text value must be a string or number");
      mutations.text += 1;
      if (phase === "update") frameCalls += 1;
      return true;
    },
    _font(handle, path) {
      knownHandle(handle, "font");
      assert.equal(typeof path, "string", "font path must be a string");
      mutations.font += 1;
      if (phase === "update") frameCalls += 1;
      return true;
    },
    _textAntialias(handle, enabled) {
      knownHandle(handle, "text antialias");
      assert.equal(typeof enabled, "boolean", "text antialias must be boolean");
      mutations.textAntialias += 1;
      if (phase === "update") frameCalls += 1;
      return true;
    },
    _sdfGeometry(handle, kind, width, height, radius) {
      knownHandle(handle, "SDF geometry");
      assert.ok(["circle", "rounded", "rect"].includes(kind));
      finite("SDF geometry", [width, height, radius]);
      assert.ok(width > 0 && height > 0 && radius >= 0);
      mutations.sdfGeometry += 1;
      if (phase === "update") frameCalls += 1;
      return true;
    },
    _color(handle, value) {
      return mutation("color", handle, [value]);
    },
    _effect(handle, kind, amount, scale) {
      return mutation("effect", handle, [kind, amount, scale]);
    },
    _shader(handle, vertex, fragment) {
      knownHandle(handle, "shader");
      assert.equal(typeof vertex, "string");
      if (fragment !== undefined) assert.equal(typeof fragment, "string");
      return true;
    },
    _visible(handle, value) {
      knownHandle(handle, "visible");
      assert.equal(typeof value, "boolean", "visibility must be boolean");
      mutations.visible += 1;
      if (phase === "update") frameCalls += 1;
      return true;
    },
    _opacity(handle, value) {
      assert.ok(value >= 0 && value <= 1, "opacity must be between zero and one");
      return mutation("opacity", handle, [value]);
    },
    configure(settings) {
      assert.equal(phase, "construction", "fx.configure() belongs at application construction");
      assert.equal(typeof settings, "object");
      assert.ok((settings.targetFps ?? 30) > 0, "target FPS must be positive");
    },
    debugBar(value) {
      assert.ok(typeof value === "boolean" ||
                (typeof value === "number" && Number.isFinite(value) && value >= 0),
                "debugBar() expects a boolean or non-negative minutes");
    },
    camera(...values) {
      finite("camera", values);
    },
    data(path, fallback) {
      try {
        return JSON.parse(readFileSync(confinedAsset(root, path), "utf8"));
      } catch (error) {
        if (arguments.length > 1) return fallback;
        throw error;
      }
    },
    secret(name, fallback = "") {
      assert.match(name, /^[A-Za-z0-9_-]+$/);
      return fallback;
    },
    log() {},
    product: Object.freeze({
      name: "microFX",
      slug: "microfx",
      defaultPeerId: "microfx-demo",
      defaultSetupSsid: "microfx-setup",
      defaultSetupPassword: "microfxsetup"
    }),
    effects: Object.freeze({ none: 0, gradient: 1, noise: 2, bands: 3 }),
    math: Object.freeze({
      noise2(x, y) {
        finite("noise2", [x, y]);
        const value = Math.sin(x * 12.9898 + y * 78.233) * 43758.5453;
        return value - Math.floor(value);
      }
    })
  };

  return {
    fx,
    beginFrame() {
      phase = "update";
      frameCalls = 0;
      fx._beginFrame();
    },
    endFrame() {
      fx._endFrame();
      maximumFrameCalls = Math.max(maximumFrameCalls, frameCalls);
      totalUpdateCalls += frameCalls;
    },
    report(frames) {
      return Object.freeze({
        frames,
        counts: Object.freeze({ ...counts }),
        handles: handles.size,
        mutations: Object.freeze({ ...mutations }),
        maximumFrameCalls,
        averageFrameCalls: frames ? totalUpdateCalls / frames : 0
      });
    }
  };
}

export function createAppRuntimeTest({
  root,
  source,
  filename = "main.js",
  runtimeSource,
  capacities = defaultCapacities,
  timeout = 1000
}) {
  assert.equal(typeof source, "string", "application source is required");
  assert.equal(typeof runtimeSource, "string", "retained runtime source is required");
  const native = instrumentedNativeFx(root, capacities);
  const context = vm.createContext({ fx: native.fx, width: 1920, height: 1080,
                                     console, Math, Date });
  vm.runInContext(runtimeSource, context, { filename: "retained.js", timeout });
  vm.runInContext(
    `${source}\n;globalThis.__microfxUpdate = typeof update === "function" ? update : null;`,
    context,
    { filename, timeout }
  );
  assert.equal(typeof context.__microfxUpdate, "function", `${filename}: update() missing`);

  let frames = 0;
  return Object.freeze({
    runFrame(time, delta) {
      finite("frame", [time, delta]);
      assert.ok(delta >= 0, "frame delta must not be negative");
      context.__microfxTime = time;
      context.__microfxDelta = delta;
      native.beginFrame();
      vm.runInContext("__microfxUpdate(__microfxTime, __microfxDelta)", context,
                      { filename: `${filename}:update`, timeout });
      native.endFrame();
      frames += 1;
    },
    runFrames(count, fps = 30) {
      assert.ok(Number.isInteger(count) && count >= 0, "frame count must be non-negative");
      assert.ok(Number.isFinite(fps) && fps > 0, "test FPS must be positive");
      for (let frame = 0; frame < count; frame += 1) {
        this.runFrame(frame / fps, 1 / fps);
      }
      return this.report();
    },
    report() { return native.report(frames); }
  });
}

export function loadAppRuntimeTest({ root, script, runtimeSource, ...options }) {
  return createAppRuntimeTest({
    root,
    source: readFileSync(script, "utf8"),
    filename: script,
    runtimeSource,
    ...options
  });
}
