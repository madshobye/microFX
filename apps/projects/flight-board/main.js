fx.configure({ targetFps: 60, pixelDensity: 1, debugBar: 1 });

// Change this block to move the entire sketch to another airport.
const PLACE = {
  label: "LONDON",
  airport: {
    iata: "LHR",
    icao: "EGLL",
    latitude: 51.47,
    longitude: -0.4543
  },
  mapCenter: {
    latitude: 51.49,
    longitude: -0.25
  },
  mapZoom: 11.45,
  searchRadiusNm: 25,
  airportGroundRadiusKm: 4
};

const POLL_SECONDS = 5;
const CORRECTION_SECONDS = 8;
const MAX_FLIGHTS = 50;
const DATA_URL = `https://opendata.adsb.fi/api/v3/lat/${PLACE.airport.latitude}` +
  `/lon/${PLACE.airport.longitude}/dist/${PLACE.searchRadiusNm}`;
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));

const map = scene.add(fx.tileMap({
  source: {
    url: "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    tileSize: 256,
    attribution: "© OpenStreetMap contributors"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 7,
  filter: {
    grayscale: 1,
    invert: 1,
    contrast: 1.35,
    brightness: 0.95,
    tint: 0x505a64ff
  }
}));
scene.add(fx.text(`${PLACE.label} AIRSPACE`, 55, 45, 28, 0x7ee5ffff));
scene.add(fx.text("ADSB.FI", 55, 1044, 11, 0x35495eff)
  .antialias(false));
scene.add(fx.text("© OPENSTREETMAP CONTRIBUTORS",
  1640, 1044, 11, 0x35495eff).antialias(false));

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapPoint = (longitude, latitude) => map.project(longitude, latitude);
const airportPoint = mapPoint(PLACE.airport.longitude, PLACE.airport.latitude);
const airportX = airportPoint.x;
const airportY = airportPoint.y;
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
    id: "", callsign: "", active: false, onGround: false,
    aircraftKind: "small", markerRadius: 7,
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
const routeCache = new Map();
const routeQueue = [];
let routeInFlight = false;
let nextRouteRequestTime = 0;

function labelText(slot) {
  const route = routeCache.get(slot.callsign);
  return route ? `${slot.callsign}: ${route}` : slot.callsign;
}

function updateAirportCount(payload) {
  const landed = payload.ac.filter(row => row && row.alt_baro === "ground" &&
    Number.isFinite(row.lon) && Number.isFinite(row.lat) &&
    distanceKm(Number(row.lon), Number(row.lat), PLACE.airport.longitude,
      PLACE.airport.latitude) <= PLACE.airportGroundRadiusKm &&
    row.t !== "TWR" && !String(row.category || "").startsWith("C")).length;
  const visible = landed > 0;
  airportDot.visible(visible);
  airportCount.visible(visible).text(`x ${landed}`);
}

function positionLabel(slot) {
  const x = Math.round(slot.currentX + Math.max(24, slot.markerRadius * 2.2));
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

function isLocalAirport(airport) {
  return String(airport.iata_code || "").toUpperCase() === PLACE.airport.iata ||
    String(airport.icao_code || "").toUpperCase() === PLACE.airport.icao;
}

function routeDescription(origin, destination) {
  const originLocal = isLocalAirport(origin);
  const destinationLocal = isLocalAirport(destination);
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
  const base = mapPoint(item.longitude, item.latitude);
  const oneMetreEast = mapPoint(item.longitude + 1 /
    (111320 * Math.max(0.2, Math.cos(item.latitude * Math.PI / 180))), item.latitude);
  const oneMetreNorth = mapPoint(item.longitude, item.latitude + 1 / 111320);
  return {
    x: Math.sin(heading) * item.velocity * (oneMetreEast.x - base.x),
    y: Math.cos(heading) * item.velocity * (oneMetreNorth.y - base.y)
  };
}

function distanceKm(longitudeA, latitudeA, longitudeB, latitudeB) {
  const latitude = (latitudeA + latitudeB) * Math.PI / 360;
  const x = (longitudeB - longitudeA) * Math.cos(latitude);
  const y = latitudeB - latitudeA;
  return Math.sqrt(x * x + y * y) * 111.32;
}

function setHeading(slot, heading, speed) {
    const angle = heading * Math.PI / 180;
    const trailAngle = angle - Math.PI / 2;
    const trailScale = clamp(speed / 130, 0.3, 1.5) *
      (slot.aircraftKind === "fighter" ? 1.35 : 1);
    // Group children use absolute retained coordinates. Return the group to
    // its local origin before rebuilding its heading-relative trail layout.
    slot.marker.position(0, 0);
    slot.trail.forEach((segment, index) => {
      const distance = (34 + index * 25) * trailScale;
      const speedThreshold = 15 + index * 45;
      segment.position(-Math.sin(angle) * distance,
                       Math.cos(angle) * distance).rotation(trailAngle)
        .visible(slot.aircraftKind !== "helicopter" && speed >= speedThreshold);
    });
    slot.dot.rotation(trailAngle);
    slot.rotor.rotation(trailAngle + Math.PI / 2);
    slot.marker.position(slot.currentX, slot.currentY);
}

function aircraftKind(item) {
  const category = String(item.category || "").toUpperCase();
  const type = String(item.typeCode || "").toUpperCase();
  const words = String(item.description || "").toUpperCase();
  const call = String(item.callsign || "").toUpperCase();
  if (category === "A7" || /HELICOPTER|ROTORCRAFT/.test(words)) return "helicopter";
  if (category === "A6" || /FIGHTER|F-?1[568]|F-?35|GRIPEN|RAFALE|EUROFIGHTER/.test(words)) {
    return "fighter";
  }
  if (/^(FDX|UPS|CLX|GTI|SRR|BOX|BCS|DHK|ABW)/.test(call) ||
      /FREIGHTER|CARGO/.test(words)) return "cargo";
  if (/^(AT|DH8|DHC|SF3|F50|P28|P32|PC6|PC12|SIRA|EV97|C172)/.test(type) ||
      /TURBOPROP|PROPELLER|SKYHAWK|TECNAM|EUROSTAR|PA-28/.test(words)) {
    return "propeller";
  }
  if (category === "A1" || category === "A2") return "small";
  return "big";
}

function markerRadius(altitudeFeet) {
  const altitude = clamp(Number(altitudeFeet || 0), 0, 40000);
  return 5 + Math.sqrt(altitude / 40000) * 7;
}

function styleMarker(slot, item) {
  const radius = item.markerRadius;
  const kind = item.aircraftKind;
  const styles = {
    small: ["circle", 2, 2, 1, 0xffd55aff, 0, 0],
    propeller: ["circle", 2, 2, 1, 0xffc766ff, 2.8, 2],
    big: ["rounded", 2.2, 1.35, 0.55, 0xffd55aff, 3.0, 4],
    cargo: ["rounded", 2.35, 1.55, 0.28, 0xe8a83eff, 3.2, 6],
    helicopter: ["circle", 1.75, 1.75, 0.875, 0x143347ff, 3.2, 3],
    fighter: ["rounded", 2.5, 0.8, 0.3, 0xfff1b8ff, 2.3, 3]
  };
  const style = styles[kind] || styles.small;
  slot.aircraftKind = kind;
  slot.markerRadius = radius;
  slot.dot.shape(style[0], radius * style[1], radius * style[2],
                 radius * style[3]).color(style[4]);
  const hasWings = style[5] > 0;
  slot.rotor.shape("rounded", radius * (style[5] || 1), style[6] || 1,
                   Math.min((style[6] || 1) * 0.5, 2))
    .color(kind === "helicopter" ? 0x7ee5ffff : style[4]).visible(hasWings);
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

    const point = mapPoint(item.longitude, item.latitude);
    const x = point.x;
    const y = point.y;
    const velocity = screenVelocity(item);
    const sameAircraft = slot.active && slot.id === item.id;
    const positionTime = Number(item.positionTime || 0);
    const hasFreshPosition = !sameAircraft || positionTime > slot.positionTime;
    slot.id = item.id;
    slot.callsign = item.callsign || `AIRCRAFT ${index + 1}`;
    slot.onGround = Boolean(item.onGround);
    styleMarker(slot, item);
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
    slot.aircraftKind = "small";
    slot.positionTime = 0;
    slot.marker.visible(false);
    slot.label.visible(false);
  });

}

function normalizeFlights(payload) {
  if (!payload || !Array.isArray(payload.ac)) throw new Error("missing aircraft data");
  const snapshotTime = Number(payload.now || Date.now()) / 1000;
  return payload.ac
    .filter(row => row && Number.isFinite(row.lon) && Number.isFinite(row.lat) &&
      row.alt_baro !== "ground" && (() => {
        const point = mapPoint(Number(row.lon), Number(row.lat));
        return point.x >= -120 && point.x <= 2040 && point.y >= -120 && point.y <= 1200;
      })())
    .slice(0, flights.length)
    .map((row, index) => {
      const velocity = Math.max(0, Number(row.gs || 0)) * 0.514444;
      const heading = Number.isFinite(row.track) ? Number(row.track) : 0;
      const radians = heading * Math.PI / 180;
      const latitude = Number(row.lat);
      const age = clamp(Number(row.seen_pos || 0), 0, 30);
      const northMetres = Math.cos(radians) * velocity * age;
      const eastMetres = Math.sin(radians) * velocity * age;
      const callsign = String(row.flight || row.r || row.hex ||
        `AIRCRAFT ${index + 1}`).trim();
      const item = {
        category: row.category,
        typeCode: row.t,
        description: row.desc,
        callsign
      };
      const altitudeFeet = Number.isFinite(row.alt_baro) ? Number(row.alt_baro) :
        (Number.isFinite(row.alt_geom) ? Number(row.alt_geom) : 0);
      return {
        id: String(row.hex || `aircraft-${index}`),
        callsign,
        longitude: Number(row.lon) + eastMetres /
          (111320 * Math.max(0.2, Math.cos(latitude * Math.PI / 180))),
        latitude: latitude + northMetres / 111320,
        positionTime: Math.floor(snapshotTime - age),
        velocity,
        heading,
        aircraftKind: aircraftKind(item),
        markerRadius: markerRadius(altitudeFeet),
        onGround: false
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
    });
}

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
