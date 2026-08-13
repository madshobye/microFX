fx.configure({
  outputWidth: 1280,
  outputHeight: 720,
  pixelDensity: 1.0,
  minimumPixelDensity: 0.5,
  targetFps: 30,
  debugBar: true
});

const heading = fx.text("RETAINED 3D STRESS EXAMPLE", 64, 64, 32, 0xffde64ff);
const cubeA = fx.cube(-1.55, 1.2, 0.0, 1.15, 0xffc744ff);
const cubeB = fx.cube(1.55, 1.2, 0.0, 1.15, 0x31ccffff);
const globe = fx.sphere(0.0, 2.6, 0.0, 1.35, 0xff62b7ff);
fx.camera(0.0, 3.0, 8.5, 0.0, 1.4, 0.0, 46.0);
fx.effect(cubeA, fx.effects.gradient, 0.9, 1.0);
fx.effect(cubeB, fx.effects.bands, 0.35, 2.0);
fx.effect(globe, fx.effects.noise, 0.45, 5.0);

function update(time, delta) {
  const drift = (fx.math.noise2(time * 0.18, 2.0) - 0.5) * 0.5;
  fx.transform(cubeA, -1.55, 1.2 + drift, 0, time * 0.31, time * 0.53, 0, 1.15);
  fx.transform(cubeB, 1.55, 1.2 - drift, 0, -time * 0.27, time * 0.41, 0, 1.15);
  fx.transform(globe, 0, 2.6 + Math.sin(time * 0.7) * 0.2, 0,
               0, time * 0.22, 0, 1.35);
}
