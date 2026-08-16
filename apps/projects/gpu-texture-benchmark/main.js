fx.configure({
  targetFps: 60,
  pixelDensity: 1,
  dithering: false,
  debugBar: true,
  debugBarStyle: "compact",
  // Keep normal presentation asynchronous. Hardware profiling can be enabled
  // temporarily when exact per-pass synchronization is required.
  profiling: false,
  profileIntervalFrames: 120
});

const WIDTH = fx.width;
const HEIGHT = fx.height;
const STAGE_SECONDS = 15;

const scene = fx.scenes.add(fx.scene({ name: "gpu-texture-benchmark" }));
const map = scene.add(fx.tileMap({
  source: {
    url: "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    tileSize: 256,
    attribution: "Imagery © Esri, Maxar, Earthstar Geographics"
  },
  center: [12.5683, 55.6761],
  zoom: 10,
  cacheDays: 7,
  filter: { grayscale: 0, contrast: 1, brightness: 1, tint: 0xffffffff }
}));

const texturePass = fx.texture(map)
  .shader("assets/shaders/texture-benchmark.fs")
  .stage("overlay");

const title = scene.add(fx.text("", 42, 90, 52, 0xeeeeeeff));
const note = scene.add(fx.text("", 44, 145, 24, 0xaaaaaaff));
scene.add(fx.text("Map data: Esri World Imagery", 44, HEIGHT - 42, 18,
  0x777777ff));

const stages = [
  {
    name: "SOLID BACKGROUND",
    note: "Presentation baseline; no full-screen texture",
    map: false,
    pass: false,
    blend: false,
    mode: 0
  },
  {
    name: "CACHED MAP ONLY",
    note: "One cached texture; no shader overlay",
    map: true,
    pass: false,
    blend: false,
    mode: 0
  },
  {
    name: "SECOND OPAQUE PASS",
    note: "Shader replaces the hidden map: one physical pass",
    map: false,
    pass: true,
    blend: false,
    mode: 0
  },
  {
    name: "SECOND BLENDED PASS",
    note: "Same shader pass with alpha blending enabled",
    map: true,
    pass: true,
    blend: true,
    mode: 1
  }
];

let stage = -1;

function selectStage(nextStage) {
  stage = nextStage;
  const definition = stages[stage];
  title.text(definition.name);
  note.text(definition.note);
  map.visible(definition.map);
  texturePass
    .visible(definition.pass)
    .blend(definition.blend)
    .params([definition.mode]);
  fx.log("GPU_BENCH stage=" + (stage + 1) + " name=" + definition.name);
}

selectStage(0);

function update(time) {
  scene.show();
  const nextStage = Math.floor(time / STAGE_SECONDS) % stages.length;
  if (nextStage !== stage) selectStage(nextStage);
}
