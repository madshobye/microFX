fx.configure({
  targetFps: 60,
  pixelDensity: 1,
  debugBar: true,
  debugBarStyle: "compact"
});

// Change this block to move the entire sketch to another airport.
const PLACE = {
  label: "COPENHAGEN",
  airport: {
    iata: "CPH",
    icao: "EKCH",
    latitude: 55.6181,
    longitude: 12.6561,
    markerOffset: [-55, 40]
  },
  mapCenter: {
    latitude: 55.67,
    longitude: 12.635
  },
  mapZoom: 11.45,
  searchRadiusNm: 25,
  airportGroundRadiusKm: 3,
  landmarks: [
    { latitude: 55.65211374559996, longitude: 12.610871553308385,
      radius: 3, color: 0xef4444ff }
  ],
  aisBounds: [[55.36, 12.10], [55.98, 13.18]]
};

const POLL_SECONDS = 5;
const CORRECTION_SECONDS = 8;
const MAX_FLIGHTS = 50;
const MAX_AIRPORT_DOTS = 50;
const AIRPORT_CLUSTER_MIN_RADIUS = 9;
const AIRPORT_CLUSTER_MAX_RADIUS = 24;
const SYMBOLIC_SUN_DIRECTION = { x: -0.72, y: -0.69 };
const SHADOW_MIN_OFFSET = 7;
const SHADOW_MAX_OFFSET = 62;
const LANDMARK_BREATH_SECONDS = 3;
const MAX_SHIPS = 24;
const SHIP_STALE_SECONDS = 180;
const TRANSIT_POLL_SECONDS = 30;
const TRANSIT_WINDOW_SECONDS = 20;
const TRANSIT_HOLD_SECONDS = 4 * 60;
const TRANSIT_CORRECTION_SECONDS = 8;
const MAX_RAIL_TRANSIT = 224;
const MAX_BUSES = 128;
const MAX_TRAIN_MAP_PATHS = 88;
const MAX_METRO_MAP_PATHS = 32;
const RAILWAY_MERGE_PIXELS = 5;
const RAILWAY_MAX_EDGE_PIXELS = 80;
const TRANSIT_MODES = new Set([
  "HIGHSPEED_RAIL", "LONG_DISTANCE", "REGIONAL_RAIL", "SUBURBAN"
]);
const METRO_MODES = new Set(["SUBWAY"]);
const BUS_MODES = new Set(["BUS", "COACH"]);
const TRANSIT_URL = "https://api.transitous.org/api/v6/map/trips";
const TRANSIT_USER_AGENT =
  "microFX-copenhagen-map/0.1 (+https://github.com/madshobye/microFX)";
const AIS_API_KEY = fx.secret("AISSTREAM_API_KEY", "");
const AIS_URL = "wss://stream.aisstream.io/v0/stream";
const DATA_URL = `https://opendata.adsb.fi/api/v3/lat/${PLACE.airport.latitude}` +
  `/lon/${PLACE.airport.longitude}/dist/${PLACE.searchRadiusNm}`;

const mirrorVertical = left => left.concat(
  left.slice(1, -1).reverse().map(point => [-point[0], point[1]]));
const AIRCRAFT_SHAPES = {
  helicopter: mirrorVertical([[0, .947], [-.044, .863], [-.363, .961], [-.348, .902],
    [-.029, .765], [-.083, .304], [-.235, .103], [-.647, .554], [-.779, .417],
    [-.275, -.034], [-.284, -.412], [-.775, -.868], [-.632, -.995], [-.206, -.52],
    [-.069, -.667], [0, -.669]]),
  fighter: mirrorVertical([[0, .913], [-.053, .911], [-.18, .997], [-.457, .993],
    [-.453, .853], [-.193, .61], [-.771, .61], [-.761, .422], [-.176, -.184],
    [-.101, -.772], [0, -1]]),
  cargo: mirrorVertical([[0, .83], [-.011, .827], [-.363, .937], [-.394, .774],
    [-.143, .594], [-.183, .142], [-1, .26], [-.996, .01], [-.185, -.335],
    [-.185, -.687], [-.127, -.873], [0, -.931]]),
  propeller: mirrorVertical([[0, -.753], [-.279, -.751], [-.282, -.688],
    [-.077, -.683], [-.155, -.459], [-.924, -.447], [-1, -.394], [-.994, -.183],
    [-.935, -.13], [-.15, -.13], [-.091, .474], [-.39, .591], [-.367, .755],
    [0, .752]]),
  small: mirrorVertical([[0, -.988], [-.05, -.991], [-.185, -.698], [-.191, -.399],
    [-.982, -.194], [-1, .065], [-.167, .094], [-.132, .657], [-.431, .815],
    [-.431, .991], [0, .991]]),
  big: mirrorVertical([[0, -.998], [-.095, -.94], [-.151, -.789], [-.158, -.248],
    [-.798, .241], [-.805, .392], [-.766, .413], [-.151, .22], [-.134, .68],
    [-.376, .863], [-.373, .982], [-.337, .996], [0, .871]])
};
const SHIP_SHAPE = [[0, -1], [.42, -.55], [.34, .72], [0, 1],
  [-.34, .72], [-.42, -.55]];
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));

const map = scene.add(fx.tileMap({
  source: {
    url: "https://a.basemaps.cartocdn.com/dark_nolabels/{z}/{x}/{y}.png",
    tileSize: 256,
    attribution: "© OpenStreetMap contributors · © CARTO"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 7,
  filter: {
    grayscale: 1,
    invert: 0,
    contrast: 1.12,
    brightness: 0.68,
    tint: 0xffffffff
  }
}));
scene.add(fx.text(PLACE.label, 55, 45, 28, 0x606060ff));
scene.add(fx.text("OPENSTREETMAP + CARTO",
  1690, 1040, 14, 0x708090ff).antialias(false));

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapPoint = (longitude, latitude) => map.project(longitude, latitude);
const trainMapPaths = Array.from({ length: MAX_TRAIN_MAP_PATHS }, () =>
  scene.add(fx.outline([[0, 0], [1, 0]], 0, 0, 1, 1,
    0x29434aff).opacity(0.58).visible(false)));
const metroMapPaths = Array.from({ length: MAX_METRO_MAP_PATHS }, () =>
  scene.add(fx.outline([[0, 0], [1, 0]], 0, 0, 1, 1.25,
    0x6b5426ff).opacity(0.72).visible(false)));
const landmarks = PLACE.landmarks.map(landmark => {
  const point = mapPoint(landmark.longitude, landmark.latitude);
  return {
    element: scene.add(fx.circle(point.x, point.y, landmark.radius, landmark.color)),
    color: landmark.color,
    brightnessStep: -1
  };
});
const airportPoint = mapPoint(PLACE.airport.longitude, PLACE.airport.latitude);
const airportX = airportPoint.x;
const airportY = airportPoint.y;
const airportClusterX = airportX + PLACE.airport.markerOffset[0];
const airportClusterY = airportY + PLACE.airport.markerOffset[1];
const airportCirclePoints = Array.from({ length: 32 }, (_, index) => {
  const angle = index / 32 * Math.PI * 2;
  return [Math.cos(angle), Math.sin(angle)];
});
const airportRing = scene.add(fx.outline(airportCirclePoints,
  airportClusterX, airportClusterY, AIRPORT_CLUSTER_MAX_RADIUS, 1.5,
  0xffd55aff, { closed: true }).visible(false));
const airportDots = Array.from({ length: MAX_AIRPORT_DOTS }, () =>
  scene.add(fx.circle(airportClusterX, airportClusterY, 2.2, 0xffd55aff)
    .visible(false)));

// The retained slot count is bounded by the 64-element text batch. Geometry
// and labels are allocated once; network responses only update their state.
const flights = Array.from({ length: MAX_FLIGHTS }, () => {
  const marker = fx.group();
  const trail = Array.from({ length: 3 }, (_, segment) =>
    scene.add(fx.sdfRoundedRect(0, 0, 20, 5 - segment,
      (5 - segment) * 0.5, 0x38bce8ff)
      .opacity(0.8 - segment * 0.2).visible(false)));
  const trailState = trail.map(() => ({ x: 0, y: 0, spacing: 0 }));
  const shadow = scene.add(fx.polygon(AIRCRAFT_SHAPES.small,
    0, 0, 16, 0x8b98a1ff).opacity(0.60).visible(false));
  const outline = marker.add(fx.outline(AIRCRAFT_SHAPES.small,
    0, 0, 16, 1.6, 0xffd55aff, { closed: true }));
  const label = fx.text("---", 0, 0, 18, 0xffffffff).antialias(false);
  scene.add(marker);
  scene.add(label);
  return {
    id: "", callsign: "", active: false, onGround: false,
    aircraftKind: "small", markerRadius: 7, altitudeFeet: 0,
    marker, trail, trailState, trailInitialized: false, headingAngle: 0,
    shadow, outline, label,
    labelX: NaN, labelY: NaN,
    positionTime: 0,
    currentX: 0, currentY: 0, velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
});

const transit = Array.from({ length: MAX_RAIL_TRANSIT }, () => {
  const marker = scene.add(fx.sdfRoundedRect(0, 0, 18, 6, 3,
    0x65e6ffff).visible(false));
  return {
    id: "", mode: "", active: false, marker,
    lastSeen: 0,
    path: [], cumulative: [], total: 0, pathIndex: 1,
    departure: 0, arrival: 0, parked: false,
    nextPath: null, nextCumulative: null, nextTotal: 0,
    nextDeparture: 0, nextArrival: 0,
    positionInitialized: false, currentX: 0, currentY: 0,
    velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
});

const busTransit = Array.from({ length: MAX_BUSES }, () => {
  const marker = scene.add(fx.rect(0, 0, 4, 4, 0xb9c3c9ff).visible(false));
  return {
    id: "", mode: "BUS", active: false, marker,
    lastSeen: 0,
    path: [], cumulative: [], total: 0, pathIndex: 1,
    departure: 0, arrival: 0, parked: false,
    nextPath: null, nextCumulative: null, nextTotal: 0,
    nextDeparture: 0, nextArrival: 0,
    positionInitialized: false, currentX: 0, currentY: 0,
    velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
});

const movingTransit = transit.concat(busTransit);

const ships = AIS_API_KEY ? Array.from({ length: MAX_SHIPS }, () => {
  const outline = scene.add(fx.outline(SHIP_SHAPE, 0, 0, 12, 1.5,
    0x7ee5ffff, { closed: true }).visible(false));
  return {
    id: "", active: false, outline, lastSeen: 0,
    currentX: 0, currentY: 0, velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
}) : [];

let clockTime = 0;
let requestInFlight = false;
let nextRequestTime = 0;
const routeCache = new Map();
const routeQueue = [];
let routeInFlight = false;
let nextRouteRequestTime = 0;
let aisSocket = null;
let nextAisConnectTime = 0;
const shipDetails = new Map();
let transitRequestInFlight = false;
let nextTransitRequestTime = 0;
let transitJob = null;
const railwaySeen = new Set();
const railwayQueue = [];
const railwayNodes = [];
const railwayNodeCells = new Map();
const railwayEdges = new Set();
let trainMapPathCount = 0;
let metroMapPathCount = 0;
let pendingTrainMapPaths = 0;
let pendingMetroMapPaths = 0;

function brightnessColor(color, brightness) {
  const channel = shift => Math.round(clamp(((color >>> shift) & 255) * brightness,
    0, 255));
  return ((channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) |
    (color & 255)) >>> 0;
}

function updateLandmarks(time) {
  const wave = 0.85 + Math.sin(time * Math.PI * 2 / LANDMARK_BREATH_SECONDS) * 0.15;
  const step = Math.round(wave * 12);
  landmarks.forEach(landmark => {
    if (landmark.brightnessStep === step) return;
    landmark.brightnessStep = step;
    landmark.element.color(brightnessColor(landmark.color, step / 12));
  });
}

function labelText(slot) {
  const route = routeCache.get(slot.callsign);
  return route || "";
}

function updateAirportCount(payload) {
  const landed = payload.ac.filter(row => row && row.alt_baro === "ground" &&
    Number.isFinite(row.lon) && Number.isFinite(row.lat) &&
    distanceKm(Number(row.lon), Number(row.lat), PLACE.airport.longitude,
      PLACE.airport.latitude) <= PLACE.airportGroundRadiusKm &&
    row.t !== "TWR" && !String(row.category || "").startsWith("C"));
  const count = Math.min(landed.length, airportDots.length);
  const growth = clamp((count - 9) / (MAX_AIRPORT_DOTS - 9), 0, 1);
  const clusterRadius = AIRPORT_CLUSTER_MIN_RADIUS +
    (AIRPORT_CLUSTER_MAX_RADIUS - AIRPORT_CLUSTER_MIN_RADIUS) * Math.sqrt(growth);
  airportRing.scale(clusterRadius)
    .color(count > 0 ? 0xb88f32ff : 0x585858ff)
    .visible(true);
  const goldenAngle = Math.PI * (3 - Math.sqrt(5));
  airportDots.forEach((dot, index) => {
    if (index >= count) { dot.visible(false); return; }
    const radius = Math.max(2, clusterRadius - 4) *
      Math.sqrt((index + 0.5) / count);
    const angle = index * goldenAngle;
    dot.position(airportClusterX + Math.cos(angle) * radius,
      airportClusterY + Math.sin(angle) * radius).visible(true);
  });
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
  const trailScale = clamp(speed / 130, 0.3, 1.5) *
    (slot.aircraftKind === "fighter" ? 1.35 : 1);
  slot.headingAngle = angle;
  slot.trail.forEach((segment, index) => {
    slot.trailState[index].spacing = (index === 0 ? 34 : 25) * trailScale;
    const speedThreshold = 15 + index * 45;
    segment.visible(slot.aircraftKind !== "helicopter" && speed >= speedThreshold);
  });
  slot.outline.rotation(angle);
  slot.shadow.rotation(angle);
}

function positionShadow(slot) {
  const height = Math.sqrt(clamp(slot.altitudeFeet, 0, 40000) / 40000);
  const distance = SHADOW_MIN_OFFSET + height *
    (SHADOW_MAX_OFFSET - SHADOW_MIN_OFFSET);
  slot.shadow.position(slot.currentX - SYMBOLIC_SUN_DIRECTION.x * distance,
    slot.currentY - SYMBOLIC_SUN_DIRECTION.y * distance);
}

function updateTrail(slot) {
  let leaderX = slot.currentX;
  let leaderY = slot.currentY;
  const fallbackX = -Math.sin(slot.headingAngle);
  const fallbackY = Math.cos(slot.headingAngle);

  slot.trailState.forEach((link, index) => {
    const segment = slot.trail[index];
    if (!slot.trailInitialized) {
      link.x = leaderX + fallbackX * link.spacing;
      link.y = leaderY + fallbackY * link.spacing;
    } else {
      const dx = link.x - leaderX;
      const dy = link.y - leaderY;
      const distance = Math.sqrt(dx * dx + dy * dy);
      if (distance > 0.001) {
        link.x = leaderX + dx * link.spacing / distance;
        link.y = leaderY + dy * link.spacing / distance;
      } else {
        link.x = leaderX + fallbackX * link.spacing;
        link.y = leaderY + fallbackY * link.spacing;
      }
    }
    segment.position(link.x, link.y)
      .rotation(Math.atan2(leaderY - link.y, leaderX - link.x));
    leaderX = link.x;
    leaderY = link.y;
  });
  slot.trailInitialized = true;
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
    helicopter: [AIRCRAFT_SHAPES.helicopter, 0x7ee5ffff, 2.3],
    fighter: [AIRCRAFT_SHAPES.fighter, 0xfff1b8ff, 2.4],
    cargo: [AIRCRAFT_SHAPES.cargo, 0xe8a83eff, 2.5],
    propeller: [AIRCRAFT_SHAPES.propeller, 0xffc766ff, 2.4],
    small: [AIRCRAFT_SHAPES.small, 0xffd55aff, 2.3],
    big: [AIRCRAFT_SHAPES.big, 0xffd55aff, 2.5]
  };
  const style = styles[kind] || styles.small;
  const kindChanged = slot.aircraftKind !== kind;
  slot.aircraftKind = kind;
  slot.markerRadius = radius;
  if (kindChanged) {
    slot.outline.points(style[0]);
    slot.shadow.points(style[0]);
  }
  slot.outline.scale(radius * style[2]).color(style[1]);
  slot.shadow.scale(radius * style[2] * 0.94);
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
    slot.altitudeFeet = item.altitudeFeet;
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
      slot.trailInitialized = false;
    }
    if (hasFreshPosition) slot.positionTime = positionTime;
    slot.velocityX = velocity.x;
    slot.velocityY = velocity.y;
    slot.active = true;
    slot.marker.visible(true).position(slot.currentX, slot.currentY);
    slot.shadow.visible(!slot.onGround);
    positionShadow(slot);
    slot.label.visible(!slot.onGround).text(labelText(slot));
    positionLabel(slot);
    if (!slot.onGround) queueRoute(slot.callsign);
    setHeading(slot, item.heading, item.velocity);
    updateTrail(slot);
  });

  flights.forEach(slot => {
    if (assigned.has(slot)) return;
    slot.active = false;
    slot.id = "";
    slot.callsign = "";
    slot.onGround = false;
    slot.aircraftKind = "";
    slot.positionTime = 0;
    slot.trailInitialized = false;
    slot.marker.visible(false);
    slot.shadow.visible(false);
    slot.trail.forEach(segment => segment.visible(false));
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
        altitudeFeet,
        onGround: false
      };
    });
}

function shipIdentity(message, body) {
  const metadata = message.Metadata || message.MetaData || {};
  return String(body.UserID || metadata.MMSI || metadata.Mmsi || "");
}

function rememberShipDetails(message, body) {
  const id = shipIdentity(message, body);
  if (!id) return;
  const metadata = message.Metadata || message.MetaData || {};
  const previous = shipDetails.get(id) || {};
  const dimension = body.Dimension || previous.dimension || {};
  shipDetails.set(id, {
    name: String(body.Name || metadata.ShipName || previous.name || "").trim(),
    type: Number(body.Type || previous.type || 0),
    dimension
  });
}

function shipScale(id, speed) {
  const detail = shipDetails.get(id) || {};
  const dimension = detail.dimension || {};
  const length = Number(dimension.A || 0) + Number(dimension.B || 0);
  return clamp(length > 0 ? 8 + Math.sqrt(length) * .65 : 9 + Math.sqrt(speed) * .8,
    9, 20);
}

function applyShipPosition(message, body) {
  const metadata = message.Metadata || message.MetaData || {};
  const id = shipIdentity(message, body);
  const longitude = Number(body.Longitude ?? metadata.Longitude ?? metadata.longitude);
  const latitude = Number(body.Latitude ?? metadata.Latitude ?? metadata.latitude);
  if (!id || !Number.isFinite(longitude) || !Number.isFinite(latitude)) return;
  const point = mapPoint(longitude, latitude);
  if (point.x < -100 || point.x > 2020 || point.y < -100 || point.y > 1180) return;
  let slot = ships.find(value => value.active && value.id === id);
  if (!slot) slot = ships.find(value => !value.active);
  if (!slot) slot = ships.reduce((oldest, value) =>
    value.lastSeen < oldest.lastSeen ? value : oldest, ships[0]);
  const speed = Math.max(0, Number(body.Sog || 0)) * .514444;
  const heading = Number.isFinite(Number(body.Cog)) ? Number(body.Cog) :
    (Number.isFinite(Number(body.TrueHeading)) && Number(body.TrueHeading) <= 360 ?
      Number(body.TrueHeading) : 0);
  const velocity = screenVelocity({ longitude, latitude, velocity: speed, heading });
  if (slot.active && slot.id === id) {
    slot.correctionX = point.x - slot.currentX;
    slot.correctionY = point.y - slot.currentY;
    slot.correctionRemaining = 12;
  } else {
    slot.currentX = point.x;slot.currentY = point.y;
    slot.correctionX = 0;slot.correctionY = 0;slot.correctionRemaining = 0;
  }
  slot.id = id;slot.active = true;slot.lastSeen = clockTime;
  slot.velocityX = velocity.x;slot.velocityY = velocity.y;
  slot.outline.position(slot.currentX, slot.currentY)
    .rotation(heading * Math.PI / 180).scale(shipScale(id, speed)).visible(true);
}

function receiveAis(text) {
  let message;
  try { message = JSON.parse(text); } catch (_) { return; }
  if (!message) return;
  if (message.error) { fx.log(`AISSTREAM ERROR: ${message.error}`); return; }
  const type = String(message.MessageType || "");
  const body = message.Message && message.Message[type];
  if (!body) return;
  if (type === "ShipStaticData" || type === "StaticDataReport") {
    rememberShipDetails(message, body);
    return;
  }
  if (type === "PositionReport" || type === "StandardClassBPositionReport" ||
      type === "ExtendedClassBPositionReport") {
    rememberShipDetails(message, body);
    applyShipPosition(message, body);
  }
}

function connectAis() {
  if (!AIS_API_KEY || aisSocket || clockTime < nextAisConnectTime) return;
  const socket = fx.net.websocket.connect(AIS_URL);
  aisSocket = socket;
  socket.onOpen(() => socket.send(JSON.stringify({
    APIKey: AIS_API_KEY,
    BoundingBoxes: [PLACE.aisBounds],
    FilterMessageTypes: ["PositionReport", "StandardClassBPositionReport",
      "ExtendedClassBPositionReport"]
  })));
  socket.onMessage(receiveAis);
  const disconnected = () => {
    if (aisSocket !== socket) return;
    aisSocket = null;nextAisConnectTime = clockTime + 20;
  };
  socket.onClose(disconnected);
  socket.onError(disconnected);
}

function decodeTransitPath(value) {
  const points = [];
  let index = 0;
  let latitude = 0;
  let longitude = 0;
  while (index < value.length) {
    let result = 0;
    let shift = 0;
    let byte = 0;
    do {
      byte = value.charCodeAt(index++) - 63;
      result |= (byte & 31) << shift;
      shift += 5;
    } while (byte >= 32 && index < value.length);
    latitude += result & 1 ? ~(result >> 1) : result >> 1;
    result = 0;
    shift = 0;
    do {
      byte = value.charCodeAt(index++) - 63;
      result |= (byte & 31) << shift;
      shift += 5;
    } while (byte >= 32 && index < value.length);
    longitude += result & 1 ? ~(result >> 1) : result >> 1;
    points.push(mapPoint(longitude / 10000, latitude / 10000));
  }
  return points;
}

function transitPath(segment) {
  const path = decodeTransitPath(segment.polyline);
  const cumulative = [0];
  let total = 0;
  for (let index = 1; index < path.length; index++) {
    const dx = path[index].x - path[index - 1].x;
    const dy = path[index].y - path[index - 1].y;
    total += Math.sqrt(dx * dx + dy * dy);
    cumulative.push(total);
  }
  return { path, cumulative, total };
}

function rememberRailwayPath(segment, kind) {
  if (!kind || !segment || !segment.polyline) return;
  const capacity = kind === "metro" ? MAX_METRO_MAP_PATHS : MAX_TRAIN_MAP_PATHS;
  const used = kind === "metro" ? metroMapPathCount : trainMapPathCount;
  const pending = kind === "metro" ? pendingMetroMapPaths : pendingTrainMapPaths;
  const key = `${kind}:${segment.polyline}`;
  if (used >= capacity || pending >= capacity * 2 || railwaySeen.has(key)) {
    return;
  }
  railwaySeen.add(key);
  railwayQueue.push({ kind, polyline: segment.polyline });
  if (kind === "metro") pendingMetroMapPaths++;
  else pendingTrainMapPaths++;
}

function railwayNode(point) {
  const cellX = Math.floor(point.x / RAILWAY_MERGE_PIXELS);
  const cellY = Math.floor(point.y / RAILWAY_MERGE_PIXELS);
  let nearest = null;
  let nearestDistance = RAILWAY_MERGE_PIXELS * RAILWAY_MERGE_PIXELS;
  for (let y = cellY - 1; y <= cellY + 1; y++) {
    for (let x = cellX - 1; x <= cellX + 1; x++) {
      const candidates = railwayNodeCells.get(`${x}:${y}`) || [];
      candidates.forEach(candidate => {
        const dx = candidate.x - point.x;
        const dy = candidate.y - point.y;
        const distance = dx * dx + dy * dy;
        if (distance <= nearestDistance) {
          nearest = candidate;
          nearestDistance = distance;
        }
      });
    }
  }
  if (nearest) return nearest;
  const node = { id: railwayNodes.length, x: point.x, y: point.y };
  railwayNodes.push(node);
  const key = `${cellX}:${cellY}`;
  if (!railwayNodeCells.has(key)) railwayNodeCells.set(key, []);
  railwayNodeCells.get(key).push(node);
  return node;
}

function drawRailwayRun(kind, nodes) {
  if (nodes.length < 2) return;
  const paths = kind === "metro" ? metroMapPaths : trainMapPaths;
  while (nodes.length >= 2) {
    const count = kind === "metro" ? metroMapPathCount : trainMapPathCount;
    if (count >= paths.length) return;
    const chunk = nodes.slice(0, 64).map(node => [node.x, node.y]);
    paths[count].points(chunk).visible(true);
    if (kind === "metro") metroMapPathCount++;
    else trainMapPathCount++;
    if (nodes.length <= 64) return;
    nodes = nodes.slice(63);
  }
}

function processRailwayQueue() {
  if (!railwayQueue.length) return;
  const item = railwayQueue.shift();
  if (item.kind === "metro") pendingMetroMapPaths--;
  else pendingTrainMapPaths--;
  const nodes = [];
  decodeTransitPath(item.polyline).forEach(point => {
    const node = railwayNode(point);
    if (!nodes.length || nodes[nodes.length - 1].id !== node.id) nodes.push(node);
  });
  let run = [];
  for (let index = 1; index < nodes.length; index++) {
    const first = nodes[index - 1];
    const second = nodes[index];
    const dx = second.x - first.x;
    const dy = second.y - first.y;
    if (dx * dx + dy * dy > RAILWAY_MAX_EDGE_PIXELS * RAILWAY_MAX_EDGE_PIXELS) {
      drawRailwayRun(item.kind, run);
      run = [];
      continue;
    }
    const edge = first.id < second.id ?
      `${item.kind}:${first.id}:${second.id}` : `${item.kind}:${second.id}:${first.id}`;
    if (railwayEdges.has(edge)) {
      drawRailwayRun(item.kind, run);
      run = [];
      continue;
    }
    railwayEdges.add(edge);
    if (!run.length) run.push(first);
    run.push(second);
  }
  drawRailwayRun(item.kind, run);
}

function normalizeTransit(payload, now, modes, mapKind) {
  const grouped = new Map();
  payload.forEach(segment => {
    if (segment && modes.has(segment.mode) && segment.polyline) {
      rememberRailwayPath(segment, mapKind);
    }
    const trip = segment && segment.trips && segment.trips[0];
    if (!trip || !trip.tripId || !segment.polyline ||
        !modes.has(segment.mode)) return;
    if (!grouped.has(trip.tripId)) grouped.set(trip.tripId, []);
    grouped.get(trip.tripId).push(segment);
  });
  const result = [];
  grouped.forEach((segments, id) => {
    segments.sort((a, b) => Date.parse(a.departure) - Date.parse(b.departure));
    let selected = segments.find(value => Date.parse(value.departure) <= now &&
      Date.parse(value.arrival) >= now);
    let parked = false;
    let nextSegment = null;
    if (!selected) {
      for (let index = 1; index < segments.length; index++) {
        const previous = Date.parse(segments[index - 1].arrival);
        const next = Date.parse(segments[index].departure);
        if (previous <= now && next >= now && next - previous <= 10 * 60 * 1000) {
          selected = segments[index - 1];
          nextSegment = segments[index];
          parked = true;
          break;
        }
      }
    }
    if (!selected) return;
    const metrics = transitPath(selected);
    if (metrics.path.length < 2 || metrics.total < 0.1) return;
    const nextMetrics = nextSegment ? transitPath(nextSegment) : null;
    result.push({
      id, mode: selected.mode, parked,
      departure: Date.parse(selected.departure),
      arrival: Date.parse(selected.arrival),
      path: metrics.path, cumulative: metrics.cumulative, total: metrics.total,
      nextPath: nextMetrics && nextMetrics.path.length >= 2 ? nextMetrics.path : null,
      nextCumulative: nextMetrics && nextMetrics.path.length >= 2 ?
        nextMetrics.cumulative : null,
      nextTotal: nextMetrics && nextMetrics.path.length >= 2 ? nextMetrics.total : 0,
      nextDeparture: nextSegment ? Date.parse(nextSegment.departure) : 0,
      nextArrival: nextSegment ? Date.parse(nextSegment.arrival) : 0
    });
  });
  return result;
}

function mergeTransit(...groups) {
  const merged = new Map();
  groups.forEach(group => group.forEach(value => {
    if (!merged.has(value.id)) merged.set(value.id, value);
  }));
  return Array.from(merged.values());
}

function styleTransit(slot, mode) {
  slot.mode = mode;
  if (mode === "SUBWAY") {
    slot.marker.shape("circle", 5, 5, 2.5).color(0xffd55aff);
  } else {
    slot.marker.shape("circle", 6, 6, 3).color(0x65e6ffff);
  }
}

function transitPoint(value, now) {
  const progress = value.parked ? 1 : clamp(
    (now - value.departure) / Math.max(1, value.arrival - value.departure), 0, 1);
  const target = value.total * progress;
  let index = 1;
  while (index < value.cumulative.length - 1 &&
         value.cumulative[index] < target) index++;
  const previous = value.path[index - 1];
  const current = value.path[index];
  const base = value.cumulative[index - 1];
  const span = Math.max(0.001, value.cumulative[index] - base);
  const amount = clamp((target - base) / span, 0, 1);
  return {
    x: previous.x + (current.x - previous.x) * amount,
    y: previous.y + (current.y - previous.y) * amount
  };
}

function applyTransit(slots, values, styleMarkers, holdSeconds) {
  const now = Date.now();
  const assigned = new Set();
  values.forEach(value => {
    let slot = slots.find(candidate => candidate.active && candidate.id === value.id &&
      !assigned.has(candidate));
    if (!slot) slot = slots.find(candidate => !candidate.active && !assigned.has(candidate));
    if (!slot) {
      slot = slots.filter(candidate => !assigned.has(candidate)).reduce((oldest, candidate) =>
        !oldest || candidate.lastSeen < oldest.lastSeen ? candidate : oldest, null);
    }
    if (!slot) return;
    const continuing = slot.active && slot.id === value.id;
    const point = transitPoint(value, now);
    const nextPoint = transitPoint(value, now + 1000);
    assigned.add(slot);
    slot.id = value.id;
    slot.active = true;
    slot.lastSeen = now;
    slot.path = value.path;
    slot.cumulative = value.cumulative;
    slot.total = value.total;
    slot.pathIndex = 1;
    slot.departure = value.departure;
    slot.arrival = value.arrival;
    slot.parked = value.parked;
    slot.nextPath = value.nextPath;
    slot.nextCumulative = value.nextCumulative;
    slot.nextTotal = value.nextTotal;
    slot.nextDeparture = value.nextDeparture;
    slot.nextArrival = value.nextArrival;
    slot.velocityX = nextPoint.x - point.x;
    slot.velocityY = nextPoint.y - point.y;
    if (continuing && slot.positionInitialized) {
      slot.correctionX = point.x - slot.currentX;
      slot.correctionY = point.y - slot.currentY;
      slot.correctionRemaining = TRANSIT_CORRECTION_SECONDS;
    } else {
      slot.currentX = point.x;
      slot.currentY = point.y;
      slot.positionInitialized = true;
      slot.correctionX = 0;
      slot.correctionY = 0;
      slot.correctionRemaining = 0;
    }
    slot.mode = value.mode;
    if (styleMarkers) styleTransit(slot, value.mode);
    slot.marker.visible(true);
  });
  slots.forEach(slot => {
    if (assigned.has(slot)) return;
    if (slot.active && now - slot.lastSeen <= holdSeconds * 1000) return;
    slot.active = false;
    slot.id = "";
    slot.marker.visible(false);
  });
}

function requestTransit() {
  if (transitRequestInFlight || transitJob) return;
  transitRequestInFlight = true;
  const now = Date.now();
  const start = encodeURIComponent(new Date(
    now - TRANSIT_WINDOW_SECONDS * 1000).toISOString());
  const end = encodeURIComponent(new Date(
    now + TRANSIT_WINDOW_SECONDS * 1000).toISOString());
  const trainWestUrl = `${TRANSIT_URL}?zoom=8&min=55.36,12.64&max=55.98,12.10` +
    `&startTime=${start}&endTime=${end}&precision=4`;
  const trainEastUrl = `${TRANSIT_URL}?zoom=8&min=55.36,13.18&max=55.98,12.64` +
    `&startTime=${start}&endTime=${end}&precision=4`;
  const metroWestUrl = `${TRANSIT_URL}?zoom=9&min=55.64,12.565&max=55.73,12.50` +
    `&startTime=${start}&endTime=${end}&precision=4`;
  const metroEastUrl = `${TRANSIT_URL}?zoom=9&min=55.64,12.63&max=55.73,12.565` +
    `&startTime=${start}&endTime=${end}&precision=4`;
  const headers = { "User-Agent": TRANSIT_USER_AGENT };
  Promise.all([fetch(trainWestUrl, { headers }), fetch(trainEastUrl, { headers }),
    fetch(metroWestUrl, { headers }), fetch(metroEastUrl, { headers })])
    .then(responses => Promise.all(responses.map(response => {
      if (!response.ok) throw new Error(`Transitous HTTP ${response.status}`);
      return response.json();
    })))
    .then(payloads => {
      if (!payloads.every(Array.isArray)) throw new Error("invalid Transitous response");
      transitJob = {
        payloads, now: Date.now(), stage: 0,
        trains: [], metro: [], buses: []
      };
      transitRequestInFlight = false;
    })
    .catch(error => {
      fx.log(`TRANSITOUS ${error.message || error}`);
      transitJob = null;
      transitRequestInFlight = false;
      nextTransitRequestTime = clockTime + TRANSIT_POLL_SECONDS;
    });
}

function processTransitJob() {
  if (!transitJob) return;
  const job = transitJob;
  if (job.stage === 0) {
    job.trains.push(normalizeTransit(job.payloads[2], job.now, TRANSIT_MODES, "train"));
  } else if (job.stage === 1) {
    job.trains.push(normalizeTransit(job.payloads[3], job.now, TRANSIT_MODES, "train"));
  } else if (job.stage === 2) {
    job.trains.push(normalizeTransit(job.payloads[0], job.now, TRANSIT_MODES, "train"));
  } else if (job.stage === 3) {
    job.trains.push(normalizeTransit(job.payloads[1], job.now, TRANSIT_MODES, "train"));
  } else if (job.stage === 4) {
    job.metro.push(normalizeTransit(job.payloads[2], job.now, METRO_MODES, "metro"));
  } else if (job.stage === 5) {
    job.metro.push(normalizeTransit(job.payloads[3], job.now, METRO_MODES, "metro"));
  } else if (job.stage === 6) {
    job.buses.push(normalizeTransit(job.payloads[2], job.now, BUS_MODES));
  } else if (job.stage === 7) {
    job.buses.push(normalizeTransit(job.payloads[3], job.now, BUS_MODES));
  } else {
    const rail = mergeTransit(...job.trains).concat(mergeTransit(...job.metro));
    applyTransit(transit, rail.slice(0, MAX_RAIL_TRANSIT), true,
      TRANSIT_HOLD_SECONDS);
    if (rail.length > MAX_RAIL_TRANSIT) {
      fx.log(`TRANSITOUS RAIL LIMITED ${rail.length}/${MAX_RAIL_TRANSIT}`);
    }
    const buses = mergeTransit(...job.buses);
    applyTransit(busTransit, buses.slice(0, MAX_BUSES), false,
      TRANSIT_HOLD_SECONDS);
    if (buses.length > MAX_BUSES) {
      fx.log(`TRANSITOUS BUSES LIMITED ${buses.length}/${MAX_BUSES}`);
    }
    transitJob = null;
    nextTransitRequestTime = clockTime + TRANSIT_POLL_SECONDS;
    return;
  }
  job.stage++;
}

function positionTransit(slot, now, delta) {
  if (slot.parked && slot.nextPath && now >= slot.nextDeparture) {
    slot.path = slot.nextPath;
    slot.cumulative = slot.nextCumulative;
    slot.total = slot.nextTotal;
    slot.departure = slot.nextDeparture;
    slot.arrival = slot.nextArrival;
    slot.parked = false;
    slot.nextPath = null;
    slot.nextCumulative = null;
    const point = transitPoint(slot, now);
    const nextPoint = transitPoint(slot, now + 1000);
    slot.velocityX = nextPoint.x - point.x;
    slot.velocityY = nextPoint.y - point.y;
    slot.correctionX = point.x - slot.currentX;
    slot.correctionY = point.y - slot.currentY;
    slot.correctionRemaining = TRANSIT_CORRECTION_SECONDS;
  }
  const step = clamp(delta, 0, 0.25);
  if (now < slot.arrival) {
    slot.currentX += slot.velocityX * step;
    slot.currentY += slot.velocityY * step;
  }
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
  const visible = slot.currentX >= -20 && slot.currentX <= 1940 &&
    slot.currentY >= -20 && slot.currentY <= 1100;
  slot.marker.position(slot.currentX, slot.currentY).rotation(0).visible(visible);
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
requestTransit();

function update(time, delta) {
  clockTime = time;
  scene.show();
  updateLandmarks(time);
  processTransitJob();
  processRailwayQueue();
  if (!requestInFlight && time >= nextRequestTime) requestFlights();
  if (!transitRequestInFlight && !transitJob && time >= nextTransitRequestTime) {
    requestTransit();
  }
  requestNextRoute();
  connectAis();

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
    positionShadow(slot);
    updateTrail(slot);
    positionLabel(slot);
  });

  const now = Date.now();
  movingTransit.forEach(slot => {
    if (slot.active) positionTransit(slot, now, delta);
  });

  ships.forEach(slot => {
    if (!slot.active) return;
    if (time - slot.lastSeen > SHIP_STALE_SECONDS) {
      slot.active = false;slot.id = "";slot.outline.visible(false);return;
    }
    const step = clamp(delta, 0, .25);
    slot.currentX += slot.velocityX * step;
    slot.currentY += slot.velocityY * step;
    if (slot.correctionRemaining > 0) {
      const correction = Math.min(1, step / slot.correctionRemaining);
      const x = slot.correctionX * correction;
      const y = slot.correctionY * correction;
      slot.currentX += x;slot.currentY += y;
      slot.correctionX -= x;slot.correctionY -= y;
      slot.correctionRemaining = Math.max(0, slot.correctionRemaining - step);
    }
    slot.outline.position(slot.currentX, slot.currentY);
  });
}
