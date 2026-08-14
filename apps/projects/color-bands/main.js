fx.configure({ targetFps: 30, pixelDensity: 1, quality: "quality", dithering: true, debugBar: true });
const scene = fx.scenes.add(fx.scene({ name: "color-bands" }));
scene.add(fx.background(0x090d18ff, 0x020308ff));
scene.add(fx.text("COLOR / GRADIENT / DITHER", 55, 50, 32, 0xffffffff));
const palettes = [[0xff4d68ff,0x30103cff],[0x4de7ffff,0x071f40ff],[0xffd45aff,0x3a1408ff],
                  [0x74ef9aff,0x092d24ff],[0xb08cffff,0x24164aff],[0xffffffff,0x111111ff]];
const bands = palettes.map((colors, index) =>
  scene.add(fx.gradientRect(110 + index * 285, 180, 235, 720, colors[0], colors[1])));
function update(time) {
  bands.forEach((band, index) => band.position(110 + index * 285, 180 + Math.sin(time * 0.2 + index) * 30));
}
