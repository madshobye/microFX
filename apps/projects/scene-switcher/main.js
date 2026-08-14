fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: 10 });

const day = fx.scenes.add(fx.scene({ name: "day" }));
day.add(fx.background(0xe8e8e8ff, 0xbcbcbcff));
day.add(fx.text("DAY SCENE", width * 0.08, height * 0.12, 52, 0x111111ff));
const sun = day.add(fx.circle(width * 0.72, height * 0.42, 150, 0xffffffff));

const night = fx.scenes.add(fx.scene({ name: "night" }));
night.add(fx.background(0x202020ff, 0x000000ff));
night.add(fx.text("NIGHT SCENE", fx.width * 0.08, fx.height * 0.12, 52, 0xffffffff));
const moon = night.add(fx.circle(width * 0.72, height * 0.42, 115, 0xccccccff));

// Disabled retained elements stay allocated but are excluded from rendering.
night.add(fx.rect(width / 2, height / 2, width, 2, 0xffffffff)).enabled(false);

function update(time) {
  const showingDay = Math.floor(time / 5) % 2 === 0;
  (showingDay ? day : night).show();
  sun.position(width * 0.72, height * 0.42 + Math.sin(time) * 24);
  moon.position(width * 0.72, height * 0.42 + Math.cos(time) * 18);
}
