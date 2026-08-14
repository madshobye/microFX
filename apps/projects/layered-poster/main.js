fx.configure({
  targetFps: 30,
  pixelDensity: "auto",
  minimumPixelDensity: 0.5,
  quality: "balanced",
  debugBar: 10
});

const scene = fx.scenes.add(fx.scene({ name: "layered-poster" }));
scene.add(fx.background(0x17112cff, 0x03040aff));

const mark = scene.add(fx.image("assets/images/microfx-mark.png",
  960, 535, 310 / 192, 0xffffffff)).opacity(0.82);

const panels = [
  scene.add(fx.gradientRect(215, 155, 620, 690, 0x5ce4ffff, 0x163b8dff)).opacity(0.72),
  scene.add(fx.gradientRect(650, 235, 720, 650, 0xff5cbbff, 0x662184ff)).opacity(0.61),
  scene.add(fx.gradientRect(1110, 125, 570, 730, 0xffd65aff, 0x8b321fff)).opacity(0.58)
];

const rings = Array.from({ length: 18 }, (_, index) => {
  const column = index % 6;
  const row = Math.floor(index / 6);
  return scene.add(fx.circle(350 + column * 245, 270 + row * 220,
                             18 + (index % 4) * 8,
                             index % 2 ? 0xffffff80 : 0x72f0ff90));
});

scene.add(fx.text("LAYER / RHYTHM", 90, 62, 42, 0xffffffff));
scene.add(fx.text("RETAINED COMPOSITION STUDY", 92, 112, 17, 0xbcd1e8ff));
const counter = scene.add(fx.text("FRAME 0000", 1510, 1005, 17, 0xffdc6eff));
let frame = 0;

function update(time) {
  scene.show();
  frame += 1;
  mark.position(960 + Math.sin(time * 0.23) * 28,
                535 + Math.cos(time * 0.19) * 18,
                time * 0.08);
  panels.forEach((panel, index) => panel.position(
    [215, 650, 1110][index] + Math.sin(time * (0.17 + index * 0.035) + index) * 55,
    [155, 235, 125][index] + Math.cos(time * 0.13 + index) * 28));
  rings.forEach((ring, index) => ring.position(
    350 + (index % 6) * 245 + Math.sin(time * 0.5 + index * 0.7) * 36,
    270 + Math.floor(index / 6) * 220 + Math.cos(time * 0.38 + index) * 24));
  if (frame % 10 === 0) counter.text(`FRAME ${String(frame).padStart(4, "0")}`);
}
