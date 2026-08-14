import assert from "node:assert/strict";
import test from "node:test";
import {
  BlendMode, LayerKind, LayerOrigin, createLayerStack
} from "./layers.mjs";

test("retained layers expose planner-compatible descriptors", () => {
  const stack = createLayerStack();
  const scene = stack.add({
    name: "scene", logicalWidth: 1920, logicalHeight: 1080,
    kind: "scene", pixelDensity: 0.5
  });
  const ui = stack.add({
    name: "ui", logicalWidth: 1920, logicalHeight: 1080,
    kind: "ui", opacity: 0.8
  });
  const header = { type: "gradientRect" };
  assert.equal(scene.add(header), header);
  assert.throws(() => ui.add(header), /already belongs/);

  ui.opacity(0.75).blend("screen").effects(true).pixelDensity(1);
  assert.deepEqual(scene.descriptor(), {
    id: 0, name: "scene", logicalWidth: 1920, logicalHeight: 1080,
    origin: LayerOrigin.topLeft, kind: LayerKind.scene,
    blend: BlendMode.normal, opacity: 1, pixelDensity: 0.5,
    hasEffects: false, memberCount: 1
  });
  assert.deepEqual(stack.requests()[1], {
    kind: LayerKind.ui, blend: BlendMode.screen, opacity: 0.75,
    pixelDensity: 1, hasEffects: true
  });
});

test("layer stack rejects ambiguous or unsupported descriptions", () => {
  const stack = createLayerStack({ maximum: 1 });
  stack.add({ name: "only", logicalWidth: 1280, logicalHeight: 720 });
  assert.throws(() => stack.add({
    name: "extra", logicalWidth: 1280, logicalHeight: 720
  }), /full/);
  assert.throws(() => createLayerStack().add({
    name: "bad", logicalWidth: 0, logicalHeight: 720
  }), /positive integers/);
  assert.throws(() => createLayerStack().add({
    name: "bad", logicalWidth: 1280, logicalHeight: 720, pixelDensity: 0.1
  }), /pixelDensity/);
  assert.throws(() => createLayerStack().add({
    name: "bad", logicalWidth: 1280, logicalHeight: 720, blend: "mystery"
  }), /blend mode/);
});
