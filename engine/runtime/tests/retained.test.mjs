import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const source = await readFile(new URL("../retained.js", import.meta.url), "utf8");

function runtime() {
  let nextHandle = 1;
  const calls = [];
  const fx = { data: (path, fallback) => ({ path, fallback }) };
  for (const name of [
    "circle", "sdfCircle", "sdfRoundedRect", "rect", "gradientRect",
    "background", "cube", "sphere", "wireCube", "grid", "model", "text", "image"
  ]) {
    fx[`_${name}`] = (...args) => {
      calls.push([`_${name}`, ...args]);
      return nextHandle++;
    };
  }
  for (const name of ["move", "transform", "setText", "font", "textAntialias", "color", "effect", "shader", "visible", "opacity"]) {
    fx[`_${name}`] = (...args) => {
      calls.push([`_${name}`, ...args]);
      return true;
    };
  }
  fx._imageScale = (...args) => {
    calls.push(["_imageScale", ...args]);
    return true;
  };
  fx._qrMatrix = () => "111\n101\n111\n";
  vm.runInNewContext(source, { fx });
  return { fx, calls };
}

test("2D retained elements distinguish absolute position from relative move", () => {
  const { fx, calls } = runtime();
  const dot = fx.circle(10, 20, 5, 0xffffffff);
  assert.equal(dot.handle, 1);
  dot.position(100, 200).move(5, -10).rotation(0.25).rotate(0.5);
  assert.deepEqual(calls.slice(1), [
    ["_move", 1, 100, 200, 0],
    ["_move", 1, 105, 190, 0],
    ["_move", 1, 105, 190, 0.25],
    ["_move", 1, 105, 190, 0.75]
  ]);
});

test("line is a retained rotated quad without a separate renderer path", () => {
  const { fx, calls } = runtime();
  const line = fx.line(10, 20, 40, 60, 6, 0xffffffff);
  assert.equal(line.handle, 1);
  assert.deepEqual(calls, [
    ["_rect", 25, 40, 50, 6, 0xffffffff],
    ["_move", 1, 25, 40, Math.atan2(40, 30)]
  ]);
});

test("polyline validates first and constructs one retained quad batch path", () => {
  const { fx, calls } = runtime();
  const path = fx.polyline([[10, 10], { x: 20, y: 10 }, [20, 30]],
                           4, 0xffffffff, { closed: true });
  assert.equal(path.elements().length, 3);
  assert.equal(calls.filter(call => call[0] === "_rect").length, 3);
  path.position(100, 50).move(5, -5).hide();
  assert.equal(calls.filter(call => call[0] === "_move").length, 9);
  assert.equal(calls.filter(call => call[0] === "_visible").length, 3);
  assert.throws(() => fx.polyline([[0, 0], [Number.NaN, 1]], 2, 0xffffffff),
                /finite/);
  assert.throws(() => fx.polyline([[0, 0], [0, 0]], 2, 0xffffffff),
                /non-zero/);
});

test("groups translate mixed retained elements without changing renderer ownership", () => {
  const { fx, calls } = runtime();
  const dot = fx.circle(10, 20, 5, 0xffffffff);
  const label = fx.text("flight", 30, 40, 16, 0xffffffff);
  const group = fx.group(dot, label);
  assert.equal(group.position(100, 200), group);
  assert.equal(group.position(110, 190), group);
  assert.deepEqual(calls.slice(2), [
    ["_move", 1, 110, 220, 0], ["_move", 2, 130, 240, 0],
    ["_move", 1, 120, 210, 0], ["_move", 2, 140, 230, 0]
  ]);
  assert.throws(() => fx.group(dot), /already belongs/);
  assert.equal(group.hide().show(), group);
});

test("3D retained elements preserve transform state across fluent mutations", () => {
  const { fx, calls } = runtime();
  const cube = fx.cube(1, 2, 3, 4, 0xff00ffff);
  cube.move(2, 3, 4).rotation(0.1, 0.2, 0.3).scale(2);
  assert.deepEqual(calls.slice(1), [
    ["_transform", 1, 3, 5, 7, 0, 0, 0, 4],
    ["_transform", 1, 3, 5, 7, 0.1, 0.2, 0.3, 4],
    ["_transform", 1, 3, 5, 7, 0.1, 0.2, 0.3, 2]
  ]);
});

test("QR codes are generic retained groups and mesh shaders are fluent", () => {
  const { fx, calls } = runtime();
  const code = fx.qr("microfx-demo", 100, 200, 300);
  assert.ok(code.elements().length > 1);
  assert.ok(calls.some(call => call[0] === "_rect"));
  const model = fx.model("models/demo.obj", 0, 0, 0, 1, 0xffffffff)
    .shader("shaders/custom.vs", "shaders/custom.fs");
  assert.equal(model.handle > 0, true);
  assert.deepEqual(calls.at(-1),
    ["_shader", model.handle, "shaders/custom.vs", "shaders/custom.fs"]);
});

test("scene membership is explicit and constructors remain renderer-backed", () => {
  const { fx } = runtime();
  const scene = fx.scenes.add(fx.scene({ name: "demo" }));
  const label = scene.add(fx.text("hello", 4, 8, 16, 0xffffffff));
  assert.equal(scene.name, "demo");
  assert.equal(scene.elements().length, 1);
  assert.equal(scene.elements()[0], label);
  assert.equal(fx.scenes.all().length, 1);
  assert.equal(fx.scenes.all()[0], scene);
  assert.throws(() => scene.add(42), /retained element/);
});

test("scene membership accepts groups while retaining flat renderer inspection", () => {
  const { fx } = runtime();
  const scene = fx.scene({ name: "grouped" });
  const group = scene.add(fx.group(
    fx.rect(0, 0, 10, 10, 0xffffffff),
    fx.text("two", 20, 20, 12, 0xffffffff)));
  assert.equal(scene.elements()[0], group);
  assert.equal(scene.flattenedElements().length, 2);
  assert.deepEqual(scene.flattenedElements(), group.elements());
});

test("each frame disables scenes until show selects one and stable selection is a no-op", () => {
  const { fx, calls } = runtime();
  const first = fx.scenes.add(fx.scene({ name: "first" }));
  const second = fx.scenes.add(fx.scene({ name: "second" }));
  const firstItem = first.add(fx.rect(0, 0, 10, 10, 0xffffffff));
  second.add(fx.rect(20, 0, 10, 10, 0xffffffff));
  calls.length = 0;
  fx._beginFrame();
  first.show();
  fx._endFrame();
  assert.deepEqual(calls, [["_visible", firstItem.handle, true]]);
  calls.length = 0;
  fx._beginFrame();
  first.show();
  fx._endFrame();
  assert.deepEqual(calls, []);
  firstItem.enabled(false);
  assert.deepEqual(calls, [["_visible", firstItem.handle, false]]);
  calls.length = 0;
  fx._beginFrame();
  second.show();
  fx._endFrame();
  assert.deepEqual(calls, [
    ["_visible", firstItem.handle, false],
    ["_visible", 2, true]
  ]);
});

test("operation helpers accept retained objects and numeric handles", () => {
  const { fx, calls } = runtime();
  const label = fx.text("one", 0, 0, 12, 0xffffffff);
  fx.setText(label, "two");
  fx.color(label.handle, 0x12345678);
  assert.deepEqual(calls.slice(1), [
    ["_setText", 1, "two"],
    ["_color", 1, 0x12345678]
  ]);
});

test("text selects project fonts fluently and can reset to the default", () => {
  const { fx, calls } = runtime();
  const label = fx.text("one", 0, 0, 12, 0xffffffff, "fonts/display.ttf");
  assert.equal(label.font("fonts/mono.ttf"), label);
  label.font(null);
  fx.font(label, "fonts/display.ttf");
  assert.deepEqual(calls, [
    ["_text", "one", 0, 0, 12, 0xffffffff],
    ["_font", 1, "fonts/display.ttf"],
    ["_font", 1, "fonts/mono.ttf"],
    ["_font", 1, ""],
    ["_font", 1, "fonts/display.ttf"]
  ]);
  assert.throws(() => fx.rect(0, 0, 10, 10, 0xffffffff).font("bad.ttf"),
                /text elements/);
});

test("text antialiasing is configurable per retained label", () => {
  const { fx, calls } = runtime();
  const label = fx.text("one", 0, 0, 12, 0xffffffff);
  assert.equal(label.antialias(false), label);
  fx.textAntialias(label, true);
  assert.deepEqual(calls.slice(1), [
    ["_textAntialias", 1, false], ["_textAntialias", 1, true]
  ]);
  assert.throws(() => fx.rect(0, 0, 10, 10, 0xffffffff).antialias(false),
                /text elements/);
});

test("retained elements expose coherent visibility controls", () => {
  const { fx, calls } = runtime();
  const item = fx.rect(10, 20, 30, 40, 0xffffffff);
  assert.equal(item.hide(), item);
  assert.equal(item.show(), item);
  assert.equal(item.visible(false), item);
  fx.visible(item, true);
  assert.deepEqual(calls.slice(1), [
    ["_visible", 1, false], ["_visible", 1, true],
    ["_visible", 1, false], ["_visible", 1, true]
  ]);
});

test("2D and text opacity stays on retained objects", () => {
  const { fx, calls } = runtime();
  const panel = fx.gradientRect(0, 0, 100, 50, 0xffffffff, 0x000000ff);
  const label = fx.text("layer", 8, 8, 16, 0xffffffff);
  assert.equal(panel.opacity(0.4), panel);
  assert.equal(label.opacity(0.7), label);
  fx.opacity(panel, 0.8);
  assert.deepEqual(calls.slice(2), [
    ["_opacity", 1, 0.4], ["_opacity", 2, 0.7], ["_opacity", 1, 0.8]
  ]);
  assert.throws(() => fx.cube(0, 0, 0, 1, 0xffffffff).opacity(0.5),
                /2D elements/);
});

test("images preserve native proportions through one retained scale", () => {
  const { fx, calls } = runtime();
  const image = fx.image("images/logo.png", 100, 200, 1.5, 0xffffffff);
  image.move(5, -10).rotation(0.25).scale(2).opacity(0.75);
  assert.deepEqual(calls, [
    ["_image", "images/logo.png", 100, 200, 1.5, 0xffffffff],
    ["_move", 1, 105, 190, 0],
    ["_move", 1, 105, 190, 0.25],
    ["_imageScale", 1, 2],
    ["_opacity", 1, 0.75]
  ]);
});

test("images reject the obsolete width and height signature loudly", () => {
  const { fx } = runtime();
  assert.throws(
    () => fx.image("images/poster.jpg", 0, 0, 2000, 1200, 0xffffffff),
    /requires exactly 5 arguments/
  );
});

test("rgba clamps and packs channels as RRGGBBAA", () => {
  const { fx } = runtime();
  assert.equal(fx.rgba(255, 128, -2), 0xff8000ff);
  assert.equal(fx.rgba(1, 2, 3, 4), 0x01020304);
});

test("feed caches one bounded platform snapshot per activation", () => {
  const { fx } = runtime();
  let reads = 0;
  fx.data = path => { reads += 1; return { path, reads }; };
  const first = fx.feed("live.json", {});
  const second = fx.feed("live.json", {});
  assert.equal(first, second);
  assert.equal(reads, 1);
});
