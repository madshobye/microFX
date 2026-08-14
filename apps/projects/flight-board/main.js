fx.configure({ targetFps: 60, pixelDensity: 1, debugBar: 1 });

// Change this block to move the entire sketch to another airport.
const PLACE = {
  label: "COPENHAGEN",
  airport: {
    iata: "CPH",
    icao: "EKCH",
    latitude: 55.6181,
    longitude: 12.6561
  },
  mapCenter: {
    latitude: 55.67,
    longitude: 12.635
  },
  mapZoom: 11.45,
  searchRadiusNm: 25,
  airportGroundRadiusKm: 3
};

const POLL_SECONDS = 5;
const CORRECTION_SECONDS = 8;
const MAX_FLIGHTS = 50;
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
const scene = fx.scenes.add(fx.scene({ name: "flight-board" }));

const map = scene.add(fx.tileMap({
  source: {
    url: "https://a.basemaps.cartocdn.com/light_nolabels/{z}/{x}/{y}.png",
    tileSize: 256,
    attribution: "© OpenStreetMap contributors · © CARTO"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 7,
  filter: {
    grayscale: 1,
    invert: 1,
    contrast: 1.35,
    brightness: 0.84,
    tint: 0x505a64ff
  }
}));
scene.add(fx.text(`${PLACE.label} AIRSPACE`, 55, 45, 28, 0x7ee5ffff));
scene.add(fx.text("ADSB.FI", 55, 1044, 11, 0x35495eff)
  .antialias(false));
scene.add(fx.text("© OPENSTREETMAP CONTRIBUTORS · © CARTO",
  1580, 1044, 11, 0x35495eff).antialias(false));

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
  const outline = marker.add(fx.outline(AIRCRAFT_SHAPES.small,
    0, 0, 16, 1.6, 0xffd55aff, { closed: true }));
  const label = fx.text("---", 0, 0, 18, 0xffffffff).antialias(false);
  scene.add(marker);
  scene.add(label);
  return {
    id: "", callsign: "", active: false, onGround: false,
    aircraftKind: "small", markerRadius: 7,
    marker, trail, outline, label,
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
    slot.outline.rotation(angle);
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
  if (kindChanged) slot.outline.points(style[0]);
  slot.outline.scale(radius * style[2]).color(style[1]);
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
    slot.aircraftKind = "";
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
