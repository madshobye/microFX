fx.configure({ targetFps: 60, pixelDensity: 1, debugBar: 1 });

const PLACE = {
  label: "COPENHAGEN",
  center: { latitude: 55.68, longitude: 12.56 },
  mapZoom: 11.8,
  query: {
    south: 55.54,
    north: 55.82,
    west: 12.36,
    east: 12.76,
    apiZoom: 8,
    precision: 4
  }
};

const API_ROOT = "https://api.transitous.org/api/v6/map/trips";
const USER_AGENT = "microFX-train-board/0.1 (+https://github.com/madshobye/microFX)";
const POLL_SECONDS = 30;
const WINDOW_SECONDS = 60;
const MAX_TRAINS = 48;
const MODES = new Set(["LONG_DISTANCE", "REGIONAL_RAIL", "SUBURBAN"]);
const MODE_STYLE = {
  LONG_DISTANCE: { color: 0xffffffff, width: 28, height: 8 },
  REGIONAL_RAIL: { color: 0xffd55aff, width: 25, height: 8 },
  SUBURBAN: { color: 0x65e6ffff, width: 21, height: 7 }
};
const LABEL_CELL_WIDTH = 240;
const LABEL_CELL_HEIGHT = 38;
const LABEL_COLUMNS = Math.ceil(1920 / LABEL_CELL_WIDTH);
const LABEL_ROWS = Math.ceil(1080 / LABEL_CELL_HEIGHT);
const labelGrid = new Uint8Array(LABEL_COLUMNS * LABEL_ROWS);

const scene = fx.scenes.add(fx.scene({ name: "train-board" }));
const map = scene.add(fx.tileMap({
  source: {
    url: "https://a.basemaps.cartocdn.com/dark_nolabels/{z}/{x}/{y}.png",
    tileSize: 256,
    attribution: "© OpenStreetMap contributors · © CARTO"
  },
  center: [PLACE.center.longitude, PLACE.center.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 7,
  filter: { grayscale: 1, contrast: 1.08, brightness: 0.58, tint: 0xffffffff }
}));

scene.add(fx.text(`${PLACE.label} TRAINS`, 55, 45, 28, 0x7ee5ffff));
scene.add(fx.text("LIVE TIMING", 55, 84, 13, 0x65e6ffff).antialias(false));
scene.add(fx.text("SCHEDULE", 190, 84, 13, 0xffd55aff).antialias(false));
scene.add(fx.text("TRANSITOUS · REJSEPLANEN · OPENSTREETMAP", 1465, 1044,
  11, 0x35495eff).antialias(false));

const slots = Array.from({ length: MAX_TRAINS }, () => {
  const route = scene.add(fx.outline([[0, 0], [1, 0]], 0, 0, 1, 1,
    0x35536aff).opacity(0.5).visible(false));
  const marker = scene.add(fx.sdfRoundedRect(0, 0, 22, 8, 3,
    0x65e6ffff).visible(false));
  const label = scene.add(fx.text("---", 0, 0, 15, 0xffffffff)
    .antialias(false).visible(false));
  return {
    id: "", active: false, route, marker, label,
    path: [], cumulative: [], total: 0, pathIndex: 1,
    departure: 0, arrival: 0, parked: false,
    labelX: NaN, labelY: NaN, screenX: 0, screenY: 0, onScreen: false
  };
});

let clockTime = 0;
let requestInFlight = false;
let nextRequestTime = 0;

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

function decodePolyline(value, precision) {
  const scale = 10 ** precision;
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
    points.push(map.project(longitude / scale, latitude / scale));
  }
  return points;
}

function pathMetrics(points) {
  const cumulative = [0];
  let total = 0;
  for (let index = 1; index < points.length; index++) {
    const dx = points[index].x - points[index - 1].x;
    const dy = points[index].y - points[index - 1].y;
    total += Math.sqrt(dx * dx + dy * dy);
    cumulative.push(total);
  }
  return { cumulative, total };
}

function displayPath(points) {
  if (points.length <= 64) return points.map(point => [point.x, point.y]);
  const result = [];
  for (let index = 0; index < 64; index++) {
    const point = points[Math.round(index * (points.length - 1) / 63)];
    result.push([point.x, point.y]);
  }
  return result;
}

function tripId(segment) {
  return segment.trips && segment.trips[0] && segment.trips[0].tripId || "";
}

function tripName(segment) {
  return segment.trips && segment.trips[0] && segment.trips[0].displayName || "TRAIN";
}

function chooseTrips(payload, now) {
  const grouped = new Map();
  payload.forEach(segment => {
    if (!segment || !MODES.has(segment.mode) || !segment.polyline) return;
    const id = tripId(segment);
    if (!id) return;
    if (!grouped.has(id)) grouped.set(id, []);
    grouped.get(id).push(segment);
  });

  const result = [];
  grouped.forEach((segments, id) => {
    segments.sort((a, b) => Date.parse(a.departure) - Date.parse(b.departure));
    let selected = segments.find(segment =>
      Date.parse(segment.departure) <= now && Date.parse(segment.arrival) >= now);
    let parked = false;
    if (!selected) {
      for (let index = 1; index < segments.length; index++) {
        const previousArrival = Date.parse(segments[index - 1].arrival);
        const nextDeparture = Date.parse(segments[index].departure);
        if (previousArrival <= now && nextDeparture >= now &&
            nextDeparture - previousArrival <= 10 * 60 * 1000) {
          selected = segments[index - 1];
          parked = true;
          break;
        }
      }
    }
    if (!selected) return;
    const path = decodePolyline(selected.polyline, PLACE.query.precision);
    if (path.length < 2) return;
    const metrics = pathMetrics(path);
    if (metrics.total < 0.1) return;
    result.push({
      id,
      name: tripName(selected),
      destination: selected.to && selected.to.name || "NEXT STATION",
      mode: selected.mode,
      realTime: Boolean(selected.realTime),
      path,
      cumulative: metrics.cumulative,
      total: metrics.total,
      departure: Date.parse(selected.departure),
      arrival: Date.parse(selected.arrival),
      parked
    });
  });
  return result.slice(0, MAX_TRAINS);
}

function applyTrips(trips) {
  const assigned = new Set();
  trips.forEach(trip => {
    let slot = slots.find(value => value.active && value.id === trip.id &&
      !assigned.has(value));
    if (!slot) slot = slots.find(value => !value.active && !assigned.has(value));
    if (!slot) return;
    assigned.add(slot);
    const style = MODE_STYLE[trip.mode];
    slot.id = trip.id;
    slot.active = true;
    slot.path = trip.path;
    slot.cumulative = trip.cumulative;
    slot.total = trip.total;
    slot.pathIndex = 1;
    slot.departure = trip.departure;
    slot.arrival = trip.arrival;
    slot.parked = trip.parked;
    slot.route.points(displayPath(trip.path)).visible(true)
      .color(trip.realTime ? 0x31566cff : 0x51472fff);
    slot.marker.visible(true).color(trip.realTime ? style.color : 0xffd55aff);
    const destination = trip.destination.toUpperCase();
    const shortDestination = destination.length > 22 ?
      `${destination.slice(0, 21)}…` : destination;
    slot.label.text(`${trip.name}: ${shortDestination}`)
      .color(trip.realTime ? 0xffffffff : 0xffe6a1ff);
  });
  slots.forEach(slot => {
    if (assigned.has(slot)) return;
    slot.active = false;
    slot.id = "";
    slot.route.visible(false);
    slot.marker.visible(false);
    slot.label.visible(false);
  });
}

function requestUrl(now) {
  const query = PLACE.query;
  const start = new Date(now - WINDOW_SECONDS * 1000).toISOString();
  const end = new Date(now + WINDOW_SECONDS * 1000).toISOString();
  return `${API_ROOT}?zoom=${query.apiZoom}` +
    `&min=${query.south},${query.east}&max=${query.north},${query.west}` +
    `&startTime=${encodeURIComponent(start)}&endTime=${encodeURIComponent(end)}` +
    `&precision=${query.precision}`;
}

function requestTrips() {
  if (requestInFlight) return;
  requestInFlight = true;
  const now = Date.now();
  fetch(requestUrl(now), { headers: { "User-Agent": USER_AGENT } })
    .then(response => {
      if (!response.ok) throw new Error(`Transitous HTTP ${response.status}`);
      return response.json();
    })
    .then(payload => {
      if (!Array.isArray(payload)) throw new Error("invalid Transitous response");
      applyTrips(chooseTrips(payload, Date.now()));
      requestInFlight = false;
      nextRequestTime = clockTime + POLL_SECONDS;
    })
    .catch(error => {
      fx.log(`TRANSITOUS ${error.message || error}`);
      requestInFlight = false;
      nextRequestTime = clockTime + POLL_SECONDS;
    });
}

function positionTrain(slot, now) {
  const progress = slot.parked ? 1 : clamp(
    (now - slot.departure) / Math.max(1, slot.arrival - slot.departure), 0, 1);
  const target = slot.total * progress;
  while (slot.pathIndex < slot.cumulative.length - 1 &&
         slot.cumulative[slot.pathIndex] < target) slot.pathIndex++;
  while (slot.pathIndex > 1 && slot.cumulative[slot.pathIndex - 1] > target) {
    slot.pathIndex--;
  }
  const index = slot.pathIndex;
  const previous = slot.path[index - 1];
  const current = slot.path[index];
  const startDistance = slot.cumulative[index - 1];
  const span = Math.max(0.001, slot.cumulative[index] - startDistance);
  const amount = clamp((target - startDistance) / span, 0, 1);
  const x = previous.x + (current.x - previous.x) * amount;
  const y = previous.y + (current.y - previous.y) * amount;
  const angle = Math.atan2(current.y - previous.y, current.x - previous.x);
  slot.screenX = x;
  slot.screenY = y;
  slot.onScreen = x >= -20 && x <= 1940 && y >= -20 && y <= 1100;
  slot.marker.position(x, y).rotation(angle).visible(slot.onScreen);
}

function positionLabel(slot) {
  if (!slot.onScreen) {
    slot.label.visible(false);
    return;
  }
  const x = slot.screenX;
  const y = slot.screenY;
  const labelX = Math.round(x + 17);
  const labelY = Math.round(y + 9);
  const column = clamp(Math.floor(labelX / LABEL_CELL_WIDTH), 0, LABEL_COLUMNS - 1);
  const row = clamp(Math.floor(labelY / LABEL_CELL_HEIGHT), 0, LABEL_ROWS - 1);
  const cell = row * LABEL_COLUMNS + column;
  if (labelGrid[cell]) {
    slot.label.visible(false);
    return;
  }
  labelGrid[cell] = 1;
  slot.label.visible(true);
  if (labelX !== slot.labelX || labelY !== slot.labelY) {
    slot.labelX = labelX;
    slot.labelY = labelY;
    slot.label.position(labelX, labelY);
  }
}

requestTrips();

function update(time) {
  clockTime = time;
  scene.show();
  if (!requestInFlight && time >= nextRequestTime) requestTrips();
  const now = Date.now();
  labelGrid.fill(0);
  slots.forEach(slot => {
    if (slot.active) positionTrain(slot, now);
  });
  slots.forEach(slot => {
    if (slot.active) positionLabel(slot);
  });
}
