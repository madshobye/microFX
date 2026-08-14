fx.configure({ targetFps: 30, pixelDensity: 1, debugBar: false });
const scene = fx.scenes.add(fx.scene({ name: "energy-clock" }));
scene.add(fx.background(0x0a1c20ff, 0x040809ff));
scene.add(fx.text("ENERGY / NEXT 24 HOURS", 64, 52, 34, 0xb9fff4ff));
const feed = fx.data("prices.json", { prices: [] });
const bars = Array.from({ length: 24 }, (_, hour) => {
  const price = Number(feed.prices[hour] ??
                       (0.25 + fx.math.noise2(hour * 0.31, 4.2) * 0.75));
  return { price, bar: scene.add(fx.gradientRect(82 + hour * 73, 880 - price * 570,
                                                 48, price * 570,
                                                 price > 0.7 ? 0xff685cff : 0x58e3b7ff,
                                                 0x162c36ff)) };
});
scene.add(fx.text("LOW", 80, 930, 18, 0x58e3b7ff));
scene.add(fx.text("HIGH", 1710, 930, 18, 0xff685cff));
const marker = scene.add(fx.rect(82, 900, 48, 5, 0xffffffff));
function update() {
  // This is a clock, not an animation timer: follow synchronized wall time.
  const now = new Date();
  const hour = now.getHours() + now.getMinutes() / 60;
  marker.position(82 + hour * 73, 900);
}
