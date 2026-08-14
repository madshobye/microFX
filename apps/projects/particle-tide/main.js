fx.configure({ targetFps: 30, pixelDensity: 1, debugBar: false });
const scene = fx.scenes.add(fx.scene({ name: "particle-tide" }));
scene.add(fx.background(0x041225ff, 0x020409ff));
const dots = Array.from({ length: 96 }, (_, index) => ({
  x: (index * 197) % 1920, y: (index * 353) % 1080,
  item: scene.add(fx.circle((index * 197) % 1920, (index * 353) % 1080,
                            2 + index % 5, index % 2 ? 0x5ae7ffff : 0xff70c8ff))
}));
function update(time) {
  dots.forEach((dot, index) => dot.item.position(
    (dot.x + time * (12 + index % 17)) % 1980 - 30,
    dot.y + Math.sin(time * 0.5 + index * 0.37) * 80));
}
