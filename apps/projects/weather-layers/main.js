fx.configure({
  targetFps: 60,
  pixelDensity: 1,
  debugBar: true,
  debugBarStyle: "compact"
});

// First weather-render prototype. The 26 x 14 honeycomb is deliberately a
// control field, not the intended output resolution. Later iterations can feed
// this same normalized field to a general render-texture shader API.
const COLS = 26;
const ROWS = 14;
const HEX_RADIUS = 41;
const HEX_X = Math.sqrt(3) * HEX_RADIUS;
const HEX_Y = HEX_RADIUS * 1.5;
const FIELD_LEFT = 48;
const FIELD_TOP = 112;
const STAGE_SECONDS = 12;

const STAGES = [
  "01  SATELLITE IMAGE",
  "02  RAW HONEYCOMB DATA",
  "03  HONEYCOMB PASS",
  "04  FOG OUTPUT",
  "05  SATELLITE OUTPUT"
];

const scene = fx.scenes.add(fx.scene({ name: "weather-layers" }));
const satellite = scene.add(fx.tileMap({
  source: {
    url: "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    tileSize: 256,
    attribution: "Imagery © Esri, Maxar, Earthstar Geographics"
  },
  center: [12.5683, 55.6761],
  zoom: 11.25,
  cacheDays: 7,
  filter: {
    grayscale: 0.15,
    contrast: 1.08,
    brightness: 0.72,
    tint: 0xdce3e8ff
  }
}));

const fieldBackdrop = scene.add(fx.rect(960, 540, 1920, 1080, 0x071015ff));
const compositeShade = scene.add(fx.rect(960, 540, 1920, 1080, 0x0b1726ff))
  .opacity(0.18);

const HEX = Array.from({ length: 6 }, (_, index) => {
  const angle = -Math.PI * 0.5 + index * Math.PI / 3;
  return [Math.cos(angle), Math.sin(angle)];
});

const clamp = value => Math.max(0, Math.min(1, value));
const smoothstep = value => {
  const x = clamp(value);
  return x * x * (3 - 2 * x);
};
const rgba = (red, green, blue, alpha) => fx.rgba(red, green, blue, alpha);

const field = [];
for (let row = 0; row < ROWS; row++) {
  for (let column = 0; column < COLS; column++) {
    const u = column / (COLS - 1);
    const v = row / (ROWS - 1);
    const broad = fx.math.noise2(u * 2.1 + 13.7, v * 2.1 - 4.2) * 0.5 + 0.5;
    const detail = fx.math.noise2(u * 6.8 - 7.1, v * 6.8 + 19.3) * 0.5 + 0.5;
    const fogBank = smoothstep(1 - Math.hypot(u - 0.34, v - 0.58) / 0.48);
    const rainFront = smoothstep(1 - Math.abs((u * 0.82 + v * 0.56) - 0.98) / 0.20);
    const cloud = clamp(0.12 + broad * 0.58 + rainFront * 0.30);
    const fog = clamp(fogBank * (0.42 + detail * 0.42));
    const rain = clamp(rainFront * cloud * (0.35 + detail * 0.65));
    const sun = clamp(1 - cloud * 0.78 - rain * 0.2);
    const x = FIELD_LEFT + HEX_X * (column + (row & 1 ? 0.5 : 0));
    const y = FIELD_TOP + HEX_Y * row;
    field.push({
      column, row, x, y, cloud, fog, rain, sun,
      windX: 0.72 + (broad - 0.5) * 0.18,
      windY: -0.24 + (detail - 0.5) * 0.12,
      cell: null
    });
  }
}

function neighbours(cell) {
  const offsets = cell.row & 1 ?
    [[-1, 0], [1, 0], [0, -1], [1, -1], [0, 1], [1, 1]] :
    [[-1, 0], [1, 0], [-1, -1], [0, -1], [-1, 1], [0, 1]];
  return offsets.map(offset => field[(cell.row + offset[1]) * COLS + cell.column + offset[0]])
    .filter(Boolean)
    .filter(value => Math.abs(value.column - cell.column) <= 1 &&
      Math.abs(value.row - cell.row) <= 1);
}

field.forEach(cell => {
  const nearby = neighbours(cell);
  const count = nearby.length + 2;
  cell.softCloud = (cell.cloud * 2 + nearby.reduce((sum, item) => sum + item.cloud, 0)) / count;
  cell.softFog = (cell.fog * 2 + nearby.reduce((sum, item) => sum + item.fog, 0)) / count;
  cell.softRain = (cell.rain * 2 + nearby.reduce((sum, item) => sum + item.rain, 0)) / count;
  cell.cell = scene.add(fx.polygon(HEX, cell.x, cell.y, HEX_RADIUS, 0xffffffff));
});

const stationData = [
  { x: 730, y: 500, fog: 0.72 },
  { x: 910, y: 640, fog: 0.28 },
  { x: 1135, y: 520, fog: 0.14 },
  { x: 1310, y: 720, fog: 0.42 },
  { x: 520, y: 760, fog: 0.64 }
];
const stations = stationData.map(item => scene.add(
  fx.sdfCircle(item.x, item.y, 7, 0xffcf5aff)));

const title = scene.add(fx.text("WEATHER LAYERS / COPENHAGEN", 48, 34, 25, 0xe9eef0ff)
  .antialias(false));
const subtitle = scene.add(fx.text("STATIC FIELD PROTOTYPE", 48, 72, 15, 0x8b989eff)
  .antialias(false));
const stageLabel = scene.add(fx.text(STAGES[0], 1450, 42, 18, 0xffcf5aff)
  .antialias(false));
const help = scene.add(fx.text("AUTOMATIC LAYER CHANGE / 12 SEC", 1450, 73, 13, 0x77848aff)
  .antialias(false));
const attribution = scene.add(fx.text("IMAGERY © ESRI / MAXAR / EARTHSTAR", 1450, 1044,
  12, 0x77848aff).antialias(false));

const tabs = STAGES.map((name, index) => {
  const x = 60 + index * 365;
  return {
    line: scene.add(fx.rect(x + 155, 1024, 310, 2, 0x46545aff)),
    text: scene.add(fx.text(String(index + 1).padStart(2, "0"), x, 1037, 13,
      0x66747aff).antialias(false))
  };
});

function rawColor(cell) {
  return rgba(22 + cell.sun * 112, 55 + cell.rain * 92,
    72 + cell.fog * 150, 255);
}

function passColor(cell) {
  const cloud = cell.softCloud;
  return rgba(36 + cell.sun * 86, 54 + cloud * 82,
    65 + cell.softFog * 120, 245);
}

function fogColor(cell, time) {
  const drift = fx.math.noise2(cell.column * 0.19 + time * cell.windX * 0.035,
    cell.row * 0.24 + time * cell.windY * 0.035) * 0.5 + 0.5;
  const amount = clamp(cell.softFog * 0.78 + cell.softCloud * 0.28 + drift * 0.16);
  return rgba(92 + amount * 95, 105 + amount * 98, 112 + amount * 100, 255);
}

function compositeColor(cell, time) {
  const drift = fx.math.noise2(cell.column * 0.23 + time * 0.025,
    cell.row * 0.21 - time * 0.008) * 0.5 + 0.5;
  const fog = clamp(cell.softFog * (0.68 + drift * 0.38));
  const rain = cell.softRain;
  return rgba(82 - rain * 30, 97 - rain * 24, 103 - rain * 16,
    18 + fog * 112 + cell.softCloud * 32);
}

let stage = -1;
let updateCursor = 0;
let frame = 0;

function selectStage(next, time) {
  stage = next;
  const showMap = stage === 0 || stage === 4;
  const showCells = stage !== 0;
  satellite.visible(showMap);
  fieldBackdrop.visible(!showMap);
  compositeShade.visible(stage === 4);
  field.forEach(cell => cell.cell.visible(showCells));
  stations.forEach(marker => marker.visible(stage === 1));
  stageLabel.text(STAGES[stage]);
  subtitle.text(stage === 0 ? "SOURCE / NO WEATHER" :
    stage === 1 ? "RAW VALUES / STATIONS" :
    stage === 2 ? "NEIGHBOUR BLEND + BROAD NOISE" :
    stage === 3 ? "FOG FIELD / ANIMATED DRIFT" :
    "MAP + LIGHT + ATMOSPHERE");
  tabs.forEach((tab, index) => {
    const active = index === stage;
    tab.line.color(active ? 0xffcf5aff : 0x46545aff);
    tab.text.color(active ? 0xffcf5aff : 0x66747aff);
  });
  updateCursor = 0;
  if (showCells) updateField(time, field.length);
}

function updateField(time, count) {
  for (let changed = 0; changed < count; changed++) {
    const cell = field[updateCursor];
    if (stage === 1) cell.cell.color(rawColor(cell)).opacity(0.96)
      .scale(HEX_RADIUS * 0.91);
    else if (stage === 2) cell.cell.color(passColor(cell)).opacity(0.94)
      .scale(HEX_RADIUS * 1.015);
    else if (stage === 3) cell.cell.color(fogColor(cell, time)).opacity(1)
      .scale(HEX_RADIUS * 1.015);
    else if (stage === 4) cell.cell.color(compositeColor(cell, time)).opacity(0.62)
      .scale(HEX_RADIUS * 1.02);
    updateCursor = (updateCursor + 1) % field.length;
  }
}

function update(time) {
  scene.show();
  frame += 1;
  const nextStage = Math.floor(time / STAGE_SECONDS) % STAGES.length;
  if (nextStage !== stage) selectStage(nextStage, time);
  // Retained polygon colors share one VBO. Refresh a larger slice occasionally
  // instead of dirtying that complete buffer on every displayed frame.
  if ((stage === 3 || stage === 4) && frame % 6 === 0) updateField(time, 18);
}
