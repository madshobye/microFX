fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: 10 });
const scene = fx.scenes.add(fx.scene({ name: "wire-city" }));
scene.add(fx.background(0x110a24ff, 0x020309ff));
scene.add(fx.grid(0, -1, 0, 14, 0x714fa8ff));
const towers = Array.from({ length: 14 }, (_, index) =>
  scene.add(fx.wireCube((index % 7 - 3) * 1.6, 0, (Math.floor(index / 7) - 0.5) * 3.3,
                        0.55 + index % 4 * 0.18, index % 2 ? 0xa178ffff : 0x4de2ffff)));
function update(time) {
  scene.show();
  fx.camera(Math.sin(time * 0.15) * 7, 4.5, Math.cos(time * 0.15) * 10, 0, 0, 0, 48);
  towers.forEach((tower, index) => tower.rotation(0, time * 0.08 + index * 0.1, 0));
}
