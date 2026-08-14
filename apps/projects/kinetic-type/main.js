fx.configure({ pixelDensity: "auto", minimumPixelDensity: 0.5, targetFps: 30, debugBar: 10 });
const scene = fx.scenes.add(fx.scene({ name: "kinetic-type" }));
scene.add(fx.background(0x150b2cff, 0x050713ff));
const words = ["MOVE", "TYPE", "LIGHT", "TIME"].map((word, index) =>
  scene.add(fx.text(word, 180, 210 + index * 170, 92 - index * 8,
                    [0xffdb5cff, 0x62e7ffff, 0xff63bfff, 0xa99affff][index])));
const bars = Array.from({ length: 12 }, (_, index) =>
  scene.add(fx.rect(1100, 150 + index * 68, 500, 10, 0xffffff30)));
function update(time) {
  scene.show();
  words.forEach((word, index) => word.position(180 + Math.sin(time * 0.7 + index) * 90,
                                               210 + index * 170));
  bars.forEach((bar, index) => bar.position(1100 + Math.cos(time + index * 0.4) * 180,
                                            150 + index * 68));
}
