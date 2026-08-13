// Hardware benchmark for retained text. Keep this scene deliberately small so
// changes to glyph batching can be compared without 2D SDF or retained 3D load.
fx.configure({
  outputWidth: 0,
  outputHeight: 0,
  pixelDensity: 1.0,
  minimumPixelDensity: 0.50,
  targetFps: 30,
  debugBar: true
});

fx.text(fx.product.name + " DEMO", 64, 54, 38, 0xffde64ff);
fx.text("By: Mads Hobye, www.hobye.dk", 64, 105, 20, 0x7de1ffff);
fx.text("RETAINED TEXT BENCHMARK", 64, 205, 28, 0x8de8ffff);

function update(time, delta) {
  // Text is static: the retained glyph VBO must not be rebuilt per frame.
}
