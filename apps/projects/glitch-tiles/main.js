fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: false });
const scene = fx.scenes.add(fx.scene({ name: "glitch-tiles" }));
scene.add(fx.background(0x17071fff, 0x04030aff));
scene.add(fx.text("SIGNAL / GLITCH", 58, 48, 36, 0xff7ce5ff));
const tiles = Array.from({ length: 36 }, (_, index) => {
  const column = index % 6, row = Math.floor(index / 6);
  return scene.add(fx.gradientRect(150 + column * 275, 170 + row * 135, 230, 96,
                                   index % 3 === 0 ? 0x42e8ffff : 0xff4ab8ff,
                                   0x211038ff));
});
function update(time) {
  tiles.forEach((tile, index) => {
    const column = index % 6, row = Math.floor(index / 6);
    const jump = fx.math.noise2(Math.floor(time * 8), index) > 0.82 ? 90 : 0;
    tile.position(150 + column * 275 + jump, 170 + row * 135);
  });
}
