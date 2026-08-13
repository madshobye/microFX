// Application/runtime policy. Environment variables remain deployment overrides.
fx.configure({
  outputWidth: 0,              // 0 + 0 selects the display's native mode
  outputHeight: 0,
  pixelDensity: "auto",       // or a fixed value from 0.25 to 1.0
  minimumPixelDensity: 0.50,
  targetFps: 30,
  debugBar: true
});

// This can also be changed from update() when an application wants its own UI.
fx.debugBar(true);

// The JavaScript application owns the complete scene. C only supplies retained
// renderer primitives and runtime services.
fx.background(0x121e3dff, 0x080c1dff);

const particles = [];
for (let i = 0; i < 20; i++) {
  const color = [0x50e1ffff, 0xff69cdff, 0xffdc6eff][i % 3];
  particles.push({
    handle: fx.circle(80 + ((i * 431) % 1760), 60 + ((i * 277) % 960),
                      2 + (i * 7) % 7, color),
    x: 80 + ((i * 431) % 1760),
    y: 60 + ((i * 277) % 960),
    speed: 0.20 + ((i * 17) % 70) / 100,
    phase: i * 0.73
  });
}

// Retained scene objects: construction happens once; update only changes state.
const header = fx.gradientRect(390, 102, 710, 142, 0x07102aff, 0x040816ff);
const appName = fx.text(fx.product.name + " DEMO", 64, 54, 38, 0xffde64ff);
const author = fx.text("By: Mads Hobye, www.hobye.dk", 64, 105, 20, 0x7de1ffff);
const sun = fx.circle(1530, 210, 54, 0xffd05aff);
const pulse = fx.circle(390, 220, 42, 0x4de7ffff);
const panel = fx.gradientRect(1480, 790, 280, 116, 0x493080dd, 0x251342dd);
const title = fx.text("RETAINED JS: SHAPES + TEXT + 3D", 64, 205, 28, 0x8de8ffff);
const floorGrid = fx.grid(0, 0, 0, 10, 0x7aa8d0ff);
const demoModel = fx.model("icosahedron.obj", 0, 1.85, 0, 3.4, 0x65d9ffff);
const orbitCubes = [];
for (let i = 0; i < 6; i++) {
  const cube = fx.cube(0, 0, 0, 0.4 + (i % 3) * 0.1,
                       i % 2 ? 0x29ccffff : 0xffc240ff);
  fx.effect(cube, fx.effects.gradient, 0.8, 1.0);
  orbitCubes.push(cube);
}
const sideCube = fx.cube(-4.5, 0.7, -2.0, 1.1, 0x6159f5ff);
const sideSphere = fx.sphere(4.2, 0.75, 2.0, 1.5, 0x3de69fff);
fx.effect(sideCube, fx.effects.gradient, 0.85, 1.0);
fx.effect(sideSphere, fx.effects.gradient, 0.85, 1.0);

function update(time, delta) {
  fx.camera(1.2 * Math.sin(time * 0.10), 3.3, 9.5, 0, 1.2, 0, 48);
  fx.move(sun, 1530 + Math.sin(time * 0.35) * 55, 210, 0);
  fx.move(pulse, 390, 220 + Math.sin(time * 0.8) * 45, 0);
  fx.move(panel, 1480, 790, Math.sin(time * 0.25) * 0.035);
  fx.transform(demoModel, 0, 1.85, 0,
               0, (-8 + Math.sin(time * 0.28) * 18) * Math.PI / 180, 0, 3.4);
  for (let i = 0; i < orbitCubes.length; i++) {
    const angle = time * (0.35 + i * 0.025) + i * Math.PI * 2 / orbitCubes.length;
    const orbit = 3.0 + (i % 2) * 0.8;
    fx.transform(orbitCubes[i], Math.cos(angle) * orbit,
                 0.8 + Math.sin(time * 0.8 + i) * 0.9,
                 Math.sin(angle) * orbit, 0, angle, 0,
                 0.35 + (i % 3) * 0.10);
  }
  fx.transform(sideCube, -4.5, 0.7, -2.0, 0, -12 * Math.PI / 180, 0, 1.1);
  fx.transform(sideSphere, 4.2, 0.75, 2.0, 0, time * 0.12, 0, 1.5);
  for (let i = 0; i < particles.length; i++) {
    const p = particles[i];
    const x = p.x + Math.sin(time * p.speed + p.phase) * 34;
    const y = ((p.y - time * (18 + p.speed * 20) + 2160) % 1200) - 60;
    fx.move(p.handle, x, y, 0);
  }
}
