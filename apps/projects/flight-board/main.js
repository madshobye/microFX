fx.configure({ targetFps: 30, pixelDensity: "auto", minimumPixelDensity: 0.5, debugBar: 10 });

const DATA_URL = "https://opensky-network.org/api/states/all" +
  "?lamin=55.3&lomin=11.4&lamax=56.2&lomax=13.3";
const POLL_SECONDS = 10;
const CORRECTION_SECONDS = 3;
const MAX_FLIGHTS = 24;
const fallback = fx.data("flights.json", { flights: [] });
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));

scene.add(fx.background(0x07182dff, 0x02060dff));
scene.add(fx.text("CPH LIVE AIRSPACE", 55, 45, 28, 0x7ee5ffff));
const status = scene.add(fx.text("OFFLINE SNAPSHOT", 1530, 50, 18, 0x6e8ca8ff));
scene.add(fx.text("11.4E", 55, 1025, 16, 0x6e8ca8ff));
scene.add(fx.text("13.3E", 1780, 1025, 16, 0x6e8ca8ff));

for (let x = 100; x <= 1820; x += 344)
  scene.add(fx.line(x, 120, x, 980, 2, 0x17395980));
for (let y = 120; y <= 980; y += 172)
  scene.add(fx.line(100, y, 1820, y, 2, 0x17395980));

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapX = longitude => 100 + clamp((longitude - 11.4) / 1.9, 0, 1) * 1720;
const mapY = latitude => 980 - clamp((latitude - 55.3) / 0.9, 0, 1) * 860;

// The retained slot count is bounded by the 32-element text batch. Geometry
// and labels are allocated once; network responses only update their state.
const flights = Array.from({ length: MAX_FLIGHTS }, () => {
  const marker = fx.group();
  const trail = Array.from({ length: 3 }, (_, segment) =>
    marker.add(fx.line(-10, 0, 10, 0, 5 - segment, 0x38bce8ff)
      .opacity(0.8 - segment * 0.2)));
  marker.add(fx.circle(0, 0, 10, 0xffd55aff));
  const label = fx.text("---", 0, 0, 18, 0xffffffff);
  scene.add(marker);
  scene.add(label);
  return {
    id: "", active: false, marker, trail, label,
    currentX: 0, currentY: 0, velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
});

let clockTime = 0;
let requestInFlight = false;
let nextRequestTime = 0;
let nextLabelUpdate = 0;
let hasLiveData = false;

function screenVelocity(item) {
  const heading = item.heading * Math.PI / 180;
  const latitudeScale = 860 / 0.9;
  const longitudeScale = 1720 / 1.9;
  return {
    x: Math.sin(heading) * item.velocity /
      (111320 * Math.max(0.2, Math.cos(item.latitude * Math.PI / 180))) * longitudeScale,
    y: -Math.cos(heading) * item.velocity / 111320 * latitudeScale
  };
}

function setHeading(slot, heading) {
  const angle = heading * Math.PI / 180;
  const trailAngle = angle - Math.PI / 2;
  slot.trail.forEach((segment, index) => {
    const distance = 45 + index * 27;
    segment.position(-Math.sin(angle) * distance,
                     Math.cos(angle) * distance).rotation(trailAngle);
  });
}

function applyFlights(values, live) {
  const items = Array.isArray(values) ? values.slice(0, flights.length) : [];
  const assigned = new Set();

  items.forEach((item, index) => {
    let slot = flights.find(candidate => candidate.active && candidate.id === item.id &&
      !assigned.has(candidate));
    if (!slot) slot = flights.find(candidate => !candidate.active && !assigned.has(candidate));
    if (!slot) slot = flights.find(candidate => !assigned.has(candidate));
    if (!slot) return;
    assigned.add(slot);

    const x = mapX(item.longitude);
    const y = mapY(item.latitude);
    const velocity = screenVelocity(item);
    const wasActive = slot.active;
    slot.id = item.id;
    if (wasActive) {
      slot.correctionX = x - slot.currentX;
      slot.correctionY = y - slot.currentY;
      slot.correctionRemaining = CORRECTION_SECONDS;
    } else {
      slot.currentX = x;
      slot.currentY = y;
      slot.correctionX = 0;
      slot.correctionY = 0;
      slot.correctionRemaining = 0;
    }
    slot.velocityX = velocity.x;
    slot.velocityY = velocity.y;
    slot.active = true;
    slot.marker.visible(true).position(slot.currentX, slot.currentY);
    slot.label.visible(true).position(slot.currentX + 18, slot.currentY - 20)
      .text(item.callsign || `AIRCRAFT ${index + 1}`);
    setHeading(slot, item.heading);
  });

  flights.forEach(slot => {
    if (assigned.has(slot)) return;
    slot.active = false;
    slot.id = "";
    slot.marker.visible(false);
    slot.label.visible(false);
  });

  hasLiveData = live;
  status.text(live ? `LIVE / ${items.length} AIRCRAFT` : "OFFLINE SNAPSHOT")
    .color(live ? 0x7ee5ffff : 0x6e8ca8ff);
}

function normalizeFlights(payload) {
  if (!payload || !Array.isArray(payload.states)) throw new Error("missing flight states");
  return payload.states
    .filter(row => Array.isArray(row) && Number.isFinite(row[5]) && Number.isFinite(row[6]))
    .slice(0, flights.length)
    .map((row, index) => ({
      id: String(row[0] || `aircraft-${index}`),
      callsign: String(row[1] || row[0] || `AIRCRAFT ${index + 1}`).trim(),
      longitude: Number(row[5]),
      latitude: Number(row[6]),
      velocity: Math.max(0, Number(row[9] || 0)),
      heading: Number.isFinite(row[10]) ? Number(row[10]) : 0
    }));
}

function requestFlights() {
  if (requestInFlight) return;
  requestInFlight = true;
  fetch(DATA_URL)
    .then(response => {
      if (!response.ok) throw new Error(`flight request failed: HTTP ${response.status}`);
      return response.json();
    })
    .then(payload => {
      applyFlights(normalizeFlights(payload), true);
      requestInFlight = false;
      nextRequestTime = clockTime + POLL_SECONDS;
    })
    .catch(() => {
      requestInFlight = false;
      nextRequestTime = clockTime + POLL_SECONDS;
      status.text(hasLiveData ? "LIVE / RETRYING" : "OFFLINE SNAPSHOT")
        .color(hasLiveData ? 0xffd55aff : 0x6e8ca8ff);
    });
}

applyFlights(fallback.flights.map((item, index) => ({
  id: `fallback-${index}`,
  callsign: String(item.callsign || `AIRCRAFT ${index + 1}`),
  longitude: Number(item.longitude),
  latitude: Number(item.latitude),
  velocity: Math.max(0, Number(item.speed || 0)),
  heading: Number(item.heading || 0)
})), false);
requestFlights();

function update(time, delta) {
  clockTime = time;
  scene.show();
  if (!requestInFlight && time >= nextRequestTime) requestFlights();

  const updateLabels = time >= nextLabelUpdate;
  if (updateLabels) nextLabelUpdate = time + 0.2;
  flights.forEach(slot => {
    if (!slot.active) return;
    const step = clamp(delta, 0, 0.25);
    slot.currentX += slot.velocityX * step;
    slot.currentY += slot.velocityY * step;
    if (slot.correctionRemaining > 0) {
      const correction = Math.min(1, step / slot.correctionRemaining);
      const x = slot.correctionX * correction;
      const y = slot.correctionY * correction;
      slot.currentX += x;
      slot.currentY += y;
      slot.correctionX -= x;
      slot.correctionY -= y;
      slot.correctionRemaining = Math.max(0, slot.correctionRemaining - step);
    }
    slot.marker.position(slot.currentX, slot.currentY);
    if (updateLabels) slot.label.position(slot.currentX + 18, slot.currentY - 20);
  });
}
