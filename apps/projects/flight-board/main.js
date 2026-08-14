fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: 10 });

const DATA_URL = "https://opensky-network.org/api/states/all" +
  "?lamin=55.3&lomin=11.4&lamax=56.2&lomax=13.3";
const fallback = fx.data("flights.json", { flights: [] });
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));

scene.add(fx.background(0x07182dff, 0x02060dff));
scene.add(fx.text("CPH LIVE AIRSPACE", 55, 45, 28, 0x7ee5ffff));
const status = scene.add(fx.text("OFFLINE SNAPSHOT", 1570, 50, 18, 0x6e8ca8ff));
scene.add(fx.text("11.4E", 55, 1025, 16, 0x6e8ca8ff));
scene.add(fx.text("13.3E", 1780, 1025, 16, 0x6e8ca8ff));

for (let x = 100; x <= 1820; x += 344)
  scene.add(fx.line(x, 120, x, 980, 2, 0x17395980));
for (let y = 120; y <= 980; y += 172)
  scene.add(fx.line(100, y, 1820, y, 2, 0x17395980));
scene.add(fx.line(790, 660, 1110, 635, 16, 0xb9c7d0ff));
scene.add(fx.text("CPH", 1130, 620, 17, 0xb9c7d0ff));

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapX = longitude => 100 + clamp((longitude - 11.4) / 1.9, 0, 1) * 1720;
const mapY = latitude => 980 - clamp((latitude - 55.3) / 0.9, 0, 1) * 860;

// Eight retained slots exist before the request starts. A response can update
// their geometry and labels, but it never allocates from an async callback.
const flights = Array.from({ length: 8 }, () => {
  const group = fx.group();
  const trail = Array.from({ length: 3 }, (_, segment) =>
    group.add(fx.line(-10, 0, 10, 0, 5 - segment, 0x38bce8ff)
      .opacity(0.8 - segment * 0.2)));
  const dot = group.add(fx.circle(0, 0, 10, 0xffd55aff));
  const label = group.add(fx.text("", 0, 0, 18, 0xffffffff));
  scene.add(group);
  return { group, trail, dot, label, heading: 0, speed: 50, active: false };
});

function applyFlights(values, live) {
  const items = Array.isArray(values) ? values.slice(0, flights.length) : [];
  flights.forEach((slot, index) => {
    const item = items[index];
    slot.group.position(0, 0);
    slot.active = Boolean(item);
    slot.group.visible(slot.active);
    if (!item) return;

    const heading = Number(item.heading || 0) * Math.PI / 180;
    const x = mapX(Number(item.longitude));
    const y = mapY(Number(item.latitude));
    const trailAngle = heading - Math.PI / 2;
    slot.heading = heading;
    slot.speed = Math.max(20, Math.min(120, Number(item.speed || 50)));
    slot.dot.position(x, y);
    slot.label.position(x + 18, y - 20).text(String(item.callsign || `CPH${index + 1}`));
    slot.trail.forEach((segment, trailIndex) => {
      const distance = 45 + trailIndex * 27;
      segment.position(x - Math.sin(heading) * distance,
                       y + Math.cos(heading) * distance).rotation(trailAngle);
    });
  });
  status.text(live ? "LIVE / OPENSKY" : "OFFLINE SNAPSHOT")
    .color(live ? 0x7ee5ffff : 0x6e8ca8ff);
}

function normalizeFlights(payload) {
  if (!payload || !Array.isArray(payload.states)) throw new Error("missing flight states");
  return payload.states
    .filter(row => Array.isArray(row) && Number.isFinite(row[5]) && Number.isFinite(row[6]))
    .slice(0, flights.length)
    .map((row, index) => ({
      callsign: String(row[1] || row[0] || `CPH${index + 1}`).trim(),
      longitude: row[5],
      latitude: row[6],
      heading: Number.isFinite(row[10]) ? row[10] : 0,
      speed: Math.max(20, Math.min(120, Number(row[9] || 50) * 0.55))
    }));
}

applyFlights(fallback.flights, false);
fetch(DATA_URL)
  .then(response => {
    if (!response.ok) throw new Error(`flight request failed: HTTP ${response.status}`);
    return response.json();
  })
  .then(payload => applyFlights(normalizeFlights(payload), true))
  .catch(() => applyFlights(fallback.flights, false));

function update(time) {
  scene.show();
  flights.forEach(slot => {
    if (!slot.active) return;
    const travel = (time * slot.speed * 0.018) % 90;
    slot.group.position(Math.sin(slot.heading) * travel,
                        -Math.cos(slot.heading) * travel);
  });
}
