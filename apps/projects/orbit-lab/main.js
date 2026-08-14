fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: 10 });
const scene = fx.scenes.add(fx.scene({ name: "orbit-lab" }));
scene.add(fx.background(0x061325ff, 0x02040aff));
scene.add(fx.text("ORBIT LAB", 70, 55, 38, 0x8fe9ffff));
scene.add(fx.grid(0, -1.6, 0, 12, 0x426b90ff));
const core = scene.add(fx.sphere(0, 0, 0, 1.3, 0xffc74fff)).effect(fx.effects.gradient, 1, 1);
const satellites = Array.from({ length: 10 }, (_, index) =>
  scene.add(fx.wireCube(0, 0, 0, 0.35 + index % 3 * 0.1, 0x65ddffff)));
function update(time) {
  scene.show();
  fx.camera(Math.sin(time * 0.12) * 2, 4.2, 11, 0, 0, 0, 48);
  core.rotation(0, time * 0.2, 0);
  satellites.forEach((object, index) => {
    const angle = time * (0.25 + index * 0.018) + index * Math.PI * 0.2;
    object.position(Math.cos(angle) * (3 + index % 2), Math.sin(angle * 1.7) * 1.2,
                    Math.sin(angle) * (3 + index % 2)).rotation(angle, angle * 1.4, 0);
  });
}
