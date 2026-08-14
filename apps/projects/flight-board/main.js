fx.configure({ targetFps: 60, pixelDensity: 1, debugBar: 1 });

const LON_MIN = 12.16;
const LON_MAX = 13.11;
const LAT_MIN = 55.53;
const LAT_MAX = 55.81;
const DATA_URL = "https://opensky-network.org/api/states/all" +
  `?lamin=${LAT_MIN}&lomin=${LON_MIN}&lamax=${LAT_MAX}&lomax=${LON_MAX}`;
const POLL_SECONDS = 10;
const CORRECTION_SECONDS = 8;
const MAX_FLIGHTS = 50;
const fallback = fx.data("flights.json", { flights: [] });
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));

scene.add(fx.backgroundImage("assets/images/copenhagen-map.png", 0xffffffff));
scene.add(fx.text("CPH LIVE AIRSPACE", 55, 45, 28, 0x7ee5ffff));
const status = scene.add(fx.text("OFFLINE SNAPSHOT", 1530, 50, 18, 0x6e8ca8ff));

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapX = longitude => clamp((longitude - LON_MIN) / (LON_MAX - LON_MIN), 0, 1) * 1920;
const mapY = latitude => (1 - clamp((latitude - LAT_MIN) / (LAT_MAX - LAT_MIN), 0, 1)) * 1080;
const airportX = mapX(12.6561);
const airportY = mapY(55.6181);
const airportDot = scene.add(fx.sdfCircle(airportX, airportY, 11, 0xffd55aff)
  .visible(false));
const airportCount = scene.add(fx.text("x 0", airportX + 20, airportY - 10,
  18, 0xffd55aff).antialias(false).visible(false));

// The retained slot count is bounded by the 64-element text batch. Geometry
// and labels are allocated once; network responses only update their state.
const flights = Array.from({ length: MAX_FLIGHTS }, () => {
  const marker = fx.group();
  const trail = Array.from({ length: 3 }, (_, segment) =>
    marker.add(fx.sdfRoundedRect(0, 0, 20, 5 - segment,
      (5 - segment) * 0.5, 0x38bce8ff)
      .opacity(0.8 - segment * 0.2)));
  const dot = marker.add(fx.sdfCircle(0, 0, 10, 0xffd55aff));
  const rotor = marker.add(fx.sdfRoundedRect(0, 0, 27, 4, 2, 0x7ee5ffff)
    .visible(false));
  const label = fx.text("---", 0, 0, 18, 0xffffffff).antialias(false);
  scene.add(marker);
  scene.add(label);
  return {
    id: "", callsign: "", active: false, onGround: false, rotorcraft: false,
    marker, trail, dot, rotor, label,
    labelX: NaN, labelY: NaN,
    positionTime: 0,
    currentX: 0, currentY: 0, velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
});

let clockTime = 0;
let requestInFlight = false;
let nextRequestTime = 0;
let hasLiveData = false;
const routeCache = new Map();
const routeQueue = [];
let routeInFlight = false;
let nextRouteRequestTime = 0;

function labelText(slot) {
  const route = routeCache.get(slot.callsign);
  return route ? `${slot.callsign}: ${route}` : slot.callsign;
}

function updateAirportCount(payload) {
  const landed = payload.states.filter(row => Array.isArray(row) &&
    row[8] === true && Number(row[17]) !== 16 && Number(row[17]) !== 17).length;
  const visible = landed > 0;
  airportDot.visible(visible);
  airportCount.visible(visible).text(`x ${landed}`);
}

function positionLabel(slot) {
  const x = Math.round(slot.currentX + 24);
  const y = Math.round(slot.currentY - 10);
  if (x === slot.labelX && y === slot.labelY) return;
  slot.labelX = x;
  slot.labelY = y;
  slot.label.position(x, y);
}

function queueRoute(callsign) {
  if (!callsign || routeCache.has(callsign) || routeQueue.includes(callsign)) return;
  routeQueue.push(callsign);
}

function finishRouteRequest(callsign, route) {
  routeCache.set(callsign, route || "");
  flights.forEach(slot => {
    if (slot.active && slot.callsign === callsign) slot.label.text(labelText(slot));
  });
  routeInFlight = false;
  nextRouteRequestTime = clockTime + 2;
}

function placeName(airport) {
  return String(airport.municipality || airport.name ||
    airport.iata_code || airport.icao_code || "").trim().toUpperCase();
}

function routeDescription(origin, destination) {
  const originLocal = origin.iata_code === "CPH" || origin.icao_code === "EKCH";
  const destinationLocal = destination.iata_code === "CPH" ||
    destination.icao_code === "EKCH";
  const from = placeName(origin);
  const to = placeName(destination);
  if (destinationLocal && from) return from;
  if (originLocal && to) return to;
  return from && to ? `${from} > ${to}` : "";
}

function requestNextRoute() {
  if (routeInFlight || clockTime < nextRouteRequestTime || !routeQueue.length) return;
  const callsign = routeQueue.shift();
  routeInFlight = true;
  fetch(`https://api.adsbdb.com/v0/callsign/${encodeURIComponent(callsign)}`)
    .then(response => {
      if (!response.ok) throw new Error(`route request failed: HTTP ${response.status}`);
      return response.json();
    })
    .then(payload => {
      const flight = payload && payload.response && payload.response.flightroute;
      const origin = flight && flight.origin;
      const destination = flight && flight.destination;
      if (!origin || !destination) {
        finishRouteRequest(callsign, "");
        return;
      }
      finishRouteRequest(callsign, routeDescription(origin, destination));
    })
    .catch(() => finishRouteRequest(callsign, ""));
}

function screenVelocity(item) {
  const heading = item.heading * Math.PI / 180;
  const latitudeScale = 1080 / (LAT_MAX - LAT_MIN);
  const longitudeScale = 1920 / (LON_MAX - LON_MIN);
  return {
    x: Math.sin(heading) * item.velocity /
      (111320 * Math.max(0.2, Math.cos(item.latitude * Math.PI / 180))) * longitudeScale,
    y: -Math.cos(heading) * item.velocity / 111320 * latitudeScale
  };
}

function setHeading(slot, heading, speed) {
    const angle = heading * Math.PI / 180;
    const trailAngle = angle - Math.PI / 2;
    const trailScale = clamp(speed / 130, 0.3, 1.5);
    // Group children use absolute retained coordinates. Return the group to
    // its local origin before rebuilding its heading-relative trail layout.
    slot.marker.position(0, 0);
    slot.trail.forEach((segment, index) => {
      const distance = (34 + index * 25) * trailScale;
      const speedThreshold = 15 + index * 45;
      segment.position(-Math.sin(angle) * distance,
                       Math.cos(angle) * distance).rotation(trailAngle)
        .visible(!slot.rotorcraft && speed >= speedThreshold);
    });
    slot.rotor.rotation(trailAngle);
    slot.marker.position(slot.currentX, slot.currentY);
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
    const sameAircraft = slot.active && slot.id === item.id;
    const positionTime = Number(item.positionTime || 0);
    const hasFreshPosition = !sameAircraft || positionTime > slot.positionTime;
    slot.id = item.id;
    slot.callsign = item.callsign || `AIRCRAFT ${index + 1}`;
    slot.onGround = Boolean(item.onGround);
    slot.rotorcraft = Boolean(item.rotorcraft);
    if (sameAircraft && hasFreshPosition) {
      slot.correctionX = x - slot.currentX;
      slot.correctionY = y - slot.currentY;
      slot.correctionRemaining = CORRECTION_SECONDS;
    } else if (!sameAircraft) {
      slot.currentX = x;
      slot.currentY = y;
      slot.correctionX = 0;
      slot.correctionY = 0;
      slot.correctionRemaining = 0;
    }
    if (hasFreshPosition) slot.positionTime = positionTime;
    slot.velocityX = velocity.x;
    slot.velocityY = velocity.y;
    slot.active = true;
    slot.marker.visible(true).position(slot.currentX, slot.currentY);
    slot.dot.color(slot.rotorcraft ? 0x143347ff : 0xffd55aff);
    slot.rotor.visible(slot.rotorcraft);
    slot.label.visible(!slot.onGround).text(labelText(slot));
    positionLabel(slot);
    if (!slot.onGround) queueRoute(slot.callsign);
    setHeading(slot, item.heading, item.velocity);
  });

  flights.forEach(slot => {
    if (assigned.has(slot)) return;
    slot.active = false;
    slot.id = "";
    slot.callsign = "";
    slot.onGround = false;
    slot.rotorcraft = false;
    slot.positionTime = 0;
    slot.marker.visible(false);
    slot.label.visible(false);
  });

  hasLiveData = live;
  status.text(live ? `LIVE / ${items.length} AIRCRAFT` : "OFFLINE SNAPSHOT")
    .color(live ? 0x7ee5ffff : 0x6e8ca8ff);
}

function normalizeFlights(payload) {
  if (!payload || !Array.isArray(payload.states)) throw new Error("missing flight states");
  const snapshotTime = Number(payload.time || 0);
  return payload.states
    .filter(row => Array.isArray(row) && Number.isFinite(row[5]) &&
      Number.isFinite(row[6]) && row[8] !== true)
    .slice(0, flights.length)
    .map((row, index) => {
      const velocity = Math.max(0, Number(row[9] || 0));
      const heading = Number.isFinite(row[10]) ? Number(row[10]) : 0;
      const radians = heading * Math.PI / 180;
      const latitude = Number(row[6]);
      const age = clamp(snapshotTime - Number(row[3] || snapshotTime), 0, 30);
      const northMetres = Math.cos(radians) * velocity * age;
      const eastMetres = Math.sin(radians) * velocity * age;
      return {
        id: String(row[0] || `aircraft-${index}`),
        callsign: String(row[1] || row[0] || `AIRCRAFT ${index + 1}`).trim(),
        longitude: Number(row[5]) + eastMetres /
          (111320 * Math.max(0.2, Math.cos(latitude * Math.PI / 180))),
        latitude: latitude + northMetres / 111320,
        positionTime: Number(row[3] || snapshotTime),
        velocity,
        heading,
        rotorcraft: Number(row[17]) === 8,
        onGround: Boolean(row[8])
      };
    });
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
      const airborne = normalizeFlights(payload);
      updateAirportCount(payload);
      applyFlights(airborne, true);
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
  heading: Number(item.heading || 0),
  positionTime: 0,
  rotorcraft: false,
  onGround: false
})), false);
requestFlights();

function update(time, delta) {
  clockTime = time;
  scene.show();
  if (!requestInFlight && time >= nextRequestTime) requestFlights();
  requestNextRoute();

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
    positionLabel(slot);
  });
}
