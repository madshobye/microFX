fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: false });
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));
scene.add(fx.background(0x07182dff, 0x02060dff));
scene.add(fx.text("CPH LIVE AIRSPACE", 55, 45, 28, 0x7ee5ffff));
scene.add(fx.text("11.4E", 55, 1025, 16, 0x6e8ca8ff));
scene.add(fx.text("13.3E", 1780, 1025, 16, 0x6e8ca8ff));

// A sparse geographic reference grid and the CPH runway remain in the same
// retained 2D quad batch as the flight trails.
for (let x = 100; x <= 1820; x += 344)
  scene.add(fx.line(x, 120, x, 980, 2, 0x17395980));
for (let y = 120; y <= 980; y += 172)
  scene.add(fx.line(100, y, 1820, y, 2, 0x17395980));
scene.add(fx.line(790, 660, 1110, 635, 16, 0xb9c7d0ff));
scene.add(fx.text("CPH", 1130, 620, 17, 0xb9c7d0ff));

const feed = fx.data("flights.json", { flights: [] });
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapX = longitude => 100 + clamp((longitude - 11.4) / 1.9, 0, 1) * 1720;
const mapY = latitude => 980 - clamp((latitude - 55.3) / 0.9, 0, 1) * 860;

const flights = feed.flights.slice(0, 8).map((item, index) => {
  const heading = Number(item.heading ?? index * 47) * Math.PI / 180;
  const x = mapX(Number(item.longitude ?? 11.55 + index * 0.23));
  const y = mapY(Number(item.latitude ?? 55.42 + (index % 4) * 0.15));
  const flight = fx.group();
  const trail = Array.from({ length: 3 }, (_, segment) => {
    const far = 34 + segment * 27;
    const near = far - 20;
    const distance = 45 + segment * 27;
    return flight.add(fx.line(
      x - Math.sin(heading) * far, y + Math.cos(heading) * far,
      x - Math.sin(heading) * near, y + Math.cos(heading) * near,
      5 - segment, 0x38bce8ff).position(
        x - Math.sin(heading) * distance,
        y + Math.cos(heading) * distance).opacity(0.8 - segment * 0.2));
  });
  const dot = flight.add(fx.circle(x, y, 10, 0xffd55aff));
  const label = flight.add(fx.text(String(item.callsign || `CPH${index + 1}`),
                                   x + 18, y - 20, 18, 0xffffffff));
  scene.add(flight);
  return {
    x, y, heading, speed: Number(item.speed ?? 50), flight, trail, dot, label
  };
});

function update(time) {
  flights.forEach(flight => {
    // Slow geographic extrapolation keeps the live snapshot legible between
    // five-minute data refreshes; it resets when the adapter reloads the app.
    const travel = (time * flight.speed * 0.018) % 90;
    flight.flight.position(Math.sin(flight.heading) * travel,
                           -Math.cos(flight.heading) * travel);
  });
}
