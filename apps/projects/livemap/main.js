fx.configure({
  targetFps: 60,
  pixelDensity: 1,
  debugBar: 2,
  debugBarStyle: "compact"
});

// Keep Odense fixed by default. Other presets remain available for manual
// experiments without rotating the installed application.
const LOCATIONS = fx.data("locations.json");
const PLACE_NAMES = ["copenhagen", "trekroner", "odense", "new-york"];
const AUTO_SWITCH_LOCATIONS = false;
const LOCATION_SWITCH_SECONDS = 60;
let placeIndex = 2;
let PLACE_NAME = PLACE_NAMES[placeIndex];
let PLACE = LOCATIONS[PLACE_NAME];
if (PLACE_NAMES.some(name => !LOCATIONS[name])) {
  throw new Error("A default location preset is missing");
}
// Keep optional feeds as explicit application switches. AIS remains disabled
// while the complete train, metro, and bus feed is enabled.
const ENABLE_AIS = false;
const ENABLE_TRANSIT = true;
// Allocate one retained pool large enough for every preset; each location can
// independently decide whether it actually requests transit data.
const HAS_TRANSIT = ENABLE_TRANSIT && PLACE_NAMES.some(name =>
  LOCATIONS[name].transit && LOCATIONS[name].transit.regions.length);
const HAS_BUSES = HAS_TRANSIT && PLACE_NAMES.some(name =>
  LOCATIONS[name].transit && LOCATIONS[name].transit.regions.some(region =>
    region.kinds.includes("bus")));
const placeHasTransit = () => ENABLE_TRANSIT && Boolean(PLACE.transit &&
  PLACE.transit.regions && PLACE.transit.regions.length);
const placeCaches = new Map(PLACE_NAMES.map(name => [name, {
  flights: [], airportCounts: [], rail: [], buses: [], railways: []
}]));
const placeCache = () => placeCaches.get(PLACE_NAME);
const configuredAirports = () => PLACE.airports || [PLACE.airport];

const POLL_SECONDS = 5;
const CORRECTION_SECONDS = 8;
const FLIGHT_INFERENCE_MIN_DISTANCE_KM = 0.02;
const FLIGHT_INFERENCE_MAX_SECONDS = 60;
const FLIGHT_INFERENCE_MAX_SPEED = 400;
const ROUTE_CACHE_SECONDS = 15 * 60;
const ROUTE_MAX_CROSS_TRACK_KM = 220;
const ROUTE_MAX_HEADING_ERROR = 80;
const MAX_FLIGHTS = 80;
const MAX_FLIGHT_LABELS = 50;
const DENSE_FLIGHT_THRESHOLD = 55;
const MAX_AIRPORTS = Math.max(...PLACE_NAMES.map(name =>
  (LOCATIONS[name].airports || [LOCATIONS[name].airport]).length));
const MAX_AIRPORT_DOTS = 60;
const MAX_AIRPORT_DOTS_PER_CLUSTER = Math.floor(MAX_AIRPORT_DOTS / MAX_AIRPORTS);
const AIRPORT_CLUSTER_MIN_RADIUS = 9;
const AIRPORT_CLUSTER_MAX_RADIUS = 24;
const SHADOW_MIN_OFFSET = 7;
const SHADOW_MAX_OFFSET = 62;
const LANDMARK_BREATH_SECONDS = 3;
// Sun position selects one opaque background. There is deliberately no
// full-screen day/night alpha transition on this hardware.
const NIGHT_SWITCH_ELEVATION = -3;
const SUN_UPDATE_SECONDS = 30;
const MAX_SHIPS = 24;
const SHIP_STALE_SECONDS = 180;
const TRANSIT_POLL_SECONDS = 30;
const TRANSIT_WINDOW_SECONDS = 20;
const TRANSIT_REQUEST_CONCURRENCY = 1;
const TRANSIT_HOLD_SECONDS = 4 * 60;
const TRANSIT_CORRECTION_SECONDS = 8;
const TRANSIT_SNAP_DISTANCE_KM = 1.5;
const MAX_RAIL_TRANSIT = 224;
const MAX_BUSES = 320;
const MAX_RAILWAY_SEGMENTS = 4096;
const RAILWAY_MERGE_PIXELS = 5;
// Transitous can encode a valid station-to-station stretch with only its end
// points. A geographic limit remains stable as presets change zoom, unlike a
// screen-pixel cutoff which erased legitimate regional tracks when zoomed in.
const RAILWAY_MAX_EDGE_KM = 35;
const TRANSIT_MODES = new Set([
  "HIGHSPEED_RAIL", "LONG_DISTANCE", "REGIONAL_RAIL", "SUBURBAN"
]);
const METRO_MODES = new Set(["SUBWAY", "TRAM", "LIGHT_RAIL"]);
const BUS_MODES = new Set(["BUS", "COACH"]);
const METRO_COLOR = 0xffd55aff;
const TRAIN_PATH_RASTER_COLOR = 0x29434aff;
const METRO_PATH_RASTER_COLOR = 0x8f742fff;
const TRAIN_COLOR = 0x65e6ffff;
const BUS_DAY_COLOR = 0xf2f5f6ff;
const BUS_NIGHT_COLOR = 0x899399ff;
const TRANSIT_URL = "https://api.transitous.org/api/v6/map/trips";
const TRANSIT_USER_AGENT =
  "microFX-copenhagen-map/0.1 (+https://github.com/madshobye/microFX)";
const AIS_API_KEY = ENABLE_AIS ? fx.secret("AISSTREAM_API_KEY", "") : "";
const AIS_URL = "wss://stream.aisstream.io/v0/stream";
const flightDataUrl = () =>
  `https://opendata.adsb.fi/api/v3/lat/${PLACE.mapCenter.latitude}` +
  `/lon/${PLACE.mapCenter.longitude}/dist/${PLACE.searchRadiusNm}`;
const RADAR = {
  listUrl: "https://opendataapi.dmi.dk/v1/radardata/collections/composite/items" +
    "?limit=4&sortorder=datetime,DESC&scanType=fullRange",
  dataset: "/dataset1/data1/data",
  stride: 4,
  threshold: 72,
  nodata: 255,
  pollSeconds: 60,
  renderCells: 192,
  scanBudget: 120,
  motionSample: 8,
  motionRange: 12,
  motionMaxSeconds: 30 * 60
};

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
const scene = fx.scenes.add(fx.scene({ name: "livemap" }));
const map = scene.add(fx.tileMap({
  source: {
    url: "https://a.basemaps.cartocdn.com/light_nolabels/{z}/{x}/{y}.png",
    tileSize: 256,
    maxZoom: 20,
    attribution: "© OpenStreetMap contributors · © CARTO"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 30,
  // Positron is intentionally subdued so the transport symbols remain clear
  // on televisions, whose picture processing otherwise makes it look washed out.
  filter: { grayscale: 0, contrast: 1.28, brightness: 0.58 }
}));
const darkMap = scene.add(fx.tileMap({
  source: {
    url: "https://a.basemaps.cartocdn.com/dark_nolabels/{z}/{x}/{y}.png",
    tileSize: 256,
    maxZoom: 20,
    attribution: "© OpenStreetMap contributors · © CARTO"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 30,
  filter: { grayscale: 0, contrast: 1, brightness: 1 }
}));
const satelliteMap = scene.add(fx.tileMap({
  source: {
    url: "https://server.arcgisonline.com/ArcGIS/rest/services/" +
      "World_Imagery/MapServer/tile/{z}/{y}/{x}",
    tileSize: 256,
    maxZoom: 20,
    attribution: "Imagery © Esri, Maxar, Earthstar Geographics"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 30,
  filter: { grayscale: 0, contrast: 1, brightness: 1 }
}));
const nightLights = scene.add(fx.tileMap({
  source: {
    url: "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/" +
      "VIIRS_CityLights_2012/default/GoogleMapsCompatible_Level8/" +
      "{z}/{y}/{x}.jpeg",
    tileSize: 256,
    maxZoom: 8,
    attribution: "Earth at Night 2012 © NASA Earth Observatory / VIIRS"
  },
  center: [PLACE.mapCenter.longitude, PLACE.mapCenter.latitude],
  zoom: PLACE.mapZoom,
  cacheDays: 3650,
  filter: { grayscale: 0, contrast: 1, brightness: 1 }
}));
map.hide();darkMap.hide();satelliteMap.hide();nightLights.hide();
const nightView = fx.texture(darkMap)
  .secondary(satelliteMap)
  .tertiary(nightLights)
  .shader("assets/shaders/weather-map.fs")
  .stage("background")
  .blend(false)
  .hide();
// Static transport topology is accumulated off-frame and uploaded as one
// transparent GPU texture. The day/night backgrounds consume it only while
// baking their opaque RGB565 caches, so steady rendering remains one pass.
const railwayTexture = HAS_TRANSIT ? fx.rasterTexture(fx.width, fx.height)
  .stage("background").blend(false).hide() : null;
if (railwayTexture) nightView.overlay(railwayTexture);
const dayView = fx.texture(map)
  .shader("assets/shaders/map-tracks.fs")
  .stage("background")
  .blend(false)
  .hide();
if (railwayTexture) dayView.overlay(railwayTexture);
const startupAssets = fx.assets.load({
  required: [map, darkMap, satelliteMap, nightLights],
  lazy: [],
  settleFrames: 2,
  loading: { label: "LOADING", x: 805, y: 515, size: 24, color: 0x777777ff }
});

// Keep the decoded radar grid as small retained cells below the moving
// symbols. This preserves holes and narrow showers; a radial blob contour
// falsely filled the empty space between distant radar samples.
const radarCells = Array.from({ length: RADAR.renderCells }, () => ({
  element: scene.add(fx.polygon([[-1, -1], [1, -1], [1, 1], [-1, 1]],
    0, 0, 1, 0x00000000).visible(false)),
  centerX: 0,
  centerY: 0
}));
const placeLabel = scene.add(fx.text(PLACE.label, 55, 45, 28, 0x606060ff));
scene.add(fx.text("ESRI / NASA / DMI / ADSB.FI / TRANSITOUS",
  1530, 1040, 14, 0x596166ff).antialias(false));

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const mapPoint = (longitude, latitude) => map.project(longitude, latitude);
const MAX_LANDMARKS = Math.max(...PLACE_NAMES.map(name =>
  (LOCATIONS[name].landmarks || []).length));
const landmarks = Array.from({ length: MAX_LANDMARKS }, () => {
  return {
    element: scene.add(fx.circle(0, 0, 3, 0xef466fff).visible(false)),
    color: 0xef466fff,
    active: false,
    brightnessStep: -1
  };
});
const airportCirclePoints = Array.from({ length: 32 }, (_, index) => {
  const angle = index / 32 * Math.PI * 2;
  return [Math.cos(angle), Math.sin(angle)];
});
const airportClusters = Array.from({ length: MAX_AIRPORTS }, () => {
  const ring = scene.add(fx.outline(airportCirclePoints, 0, 0,
    AIRPORT_CLUSTER_MAX_RADIUS, 1.5, 0xffd55aff,
    { closed: true }).visible(false));
  const dots = Array.from({ length: MAX_AIRPORT_DOTS_PER_CLUSTER }, () =>
    scene.add(fx.circle(0, 0, 2.2, 0xffd55aff).visible(false)));
  return { active: false, x: 0, y: 0, ring, dots };
});

// The retained slot count is bounded by the 64-element text batch. Geometry
// and labels are allocated once; network responses only update their state.
const flights = Array.from({ length: MAX_FLIGHTS }, (_, slotIndex) => {
  const marker = fx.group();
  const trail = Array.from({ length: 3 }, (_, segment) =>
    scene.add(fx.sdfRoundedRect(0, 0, 20, 5 - segment,
      (5 - segment) * 0.5, 0x38bce8ff)
      .opacity(0.8 - segment * 0.2).visible(false)));
  const trailState = trail.map(() => ({ x: 0, y: 0, spacing: 0 }));
  const shadow = scene.add(fx.polygon(AIRCRAFT_SHAPES.small,
    0, 0, 16, 0x020406ff).opacity(0.32).visible(false));
  const outline = marker.add(fx.polygon(AIRCRAFT_SHAPES.small,
    0, 0, 16, 0xffffffff));
  const label = slotIndex < MAX_FLIGHT_LABELS ?
    fx.text("---", 0, 0, 18, 0xffffffff).antialias(false) : null;
  scene.add(marker);
  if (label) scene.add(label);
  return {
    id: "", callsign: "", active: false, onGround: false,
    aircraftKind: "small", markerRadius: 7, altitudeFeet: 0,
    marker, trail, trailState, trailInitialized: false, headingAngle: 0,
    shadow, outline, label,
    labelX: NaN, labelY: NaN,
    longitude: 0, latitude: 0, headingDegrees: 0,
    speedMetresPerSecond: 0,
    positionTime: 0,
    currentX: 0, currentY: 0, velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
});

const transit = HAS_TRANSIT ? Array.from({ length: MAX_RAIL_TRANSIT }, () => {
  const marker = scene.add(fx.sdfRoundedRect(0, 0, 18, 6, 3,
    TRAIN_COLOR).stage("foreground").visible(false));
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
}) : [];

const busTransit = HAS_BUSES ? Array.from({ length: MAX_BUSES }, () => {
  const marker = scene.add(fx.rect(0, 0, 4, 4, BUS_DAY_COLOR).visible(false));
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
}) : [];

const movingTransit = transit.concat(busTransit);

const ships = AIS_API_KEY && PLACE.aisBounds ? Array.from({ length: MAX_SHIPS }, () => {
  const outline = scene.add(fx.outline(SHIP_SHAPE, 0, 0, 12, 1.5,
    0x7ee5ffff, { closed: true }).visible(false));
  return {
    id: "", active: false, outline, lastSeen: 0,
    currentX: 0, currentY: 0, velocityX: 0, velocityY: 0,
    correctionX: 0, correctionY: 0, correctionRemaining: 0
  };
}) : [];

let clockTime = 0;
let placeRevision = 0;
let locationSwitching = false;
let rotationStarted = false;
let nextLocationSwitch = Infinity;
let initialAssetsReady = false;
let denseFlightMode = false;
let requestInFlight = false;
let nextRequestTime = 0;
const routeCache = new Map();
const routeQueue = [];
let routeInFlight = false;
let nextRouteRequestTime = 0;
let aisSocket = null;
let nextAisConnectTime = 0;
let aisMessageCount = 0;
let aisPositionCount = 0;
const shipDetails = new Map();
let transitRequestInFlight = false;
let nextTransitRequestTime = 20;
let transitJob = null;
const railwaySeen = new Set();
const railwayQueue = [];
const railwayNodes = [];
const railwayNodeCells = new Map();
const railwayEdges = new Set();
let railwayTextureDirty = false;
let solarDirection = { x: -0.72, y: -0.69 };
let nightAmount = -1;
let nextSolarUpdate = 0;
let nextRadarPoll = 0;
let radarRequestInFlight = false;
let radarJob = null;
let radarFrameId = "";
let radarObservationEpoch = 0;
let radarVelocityX = 0;
let radarVelocityY = 0;

function updatePlaceLayout() {
  placeLabel.text(PLACE.label).visible(true);
  const configured = PLACE.landmarks || [];
  landmarks.forEach((slot, index) => {
    const landmark = configured[index];
    if (!landmark) {
      slot.active = false;
      slot.element.visible(false);
      return;
    }
    const point = mapPoint(landmark.longitude, landmark.latitude);
    slot.active = true;
    slot.color = landmark.color;
    slot.brightnessStep = -1;
    slot.element.position(point.x, point.y).color(landmark.color).visible(true);
  });
  const airports = configuredAirports();
  airportClusters.forEach((cluster, index) => {
    const airport = airports[index];
    if (!airport) {
      cluster.active = false;cluster.ring.visible(false);
      cluster.dots.forEach(dot => dot.visible(false));
      return;
    }
    const point = mapPoint(airport.longitude, airport.latitude);
    cluster.active = true;
    cluster.x = point.x + airport.markerOffset[0];
    cluster.y = point.y + airport.markerOffset[1];
    cluster.ring.position(cluster.x, cluster.y).visible(false);
    cluster.dots.forEach(dot => dot.position(cluster.x, cluster.y).visible(false));
  });
}

function clearLocationData() {
  flights.forEach(slot => {
    slot.active = false;slot.id = "";slot.callsign = "";
    slot.positionTime = 0;slot.speedMetresPerSecond = 0;
    slot.correctionRemaining = 0;slot.trailInitialized = false;
    slot.marker.visible(false);slot.shadow.visible(false);
    slot.trail.forEach(segment => segment.visible(false));
    if (slot.label) slot.label.visible(false);
  });
  movingTransit.forEach(slot => {
    slot.active = false;slot.id = "";slot.positionInitialized = false;
    slot.marker.visible(false);
  });
  ships.forEach(slot => {
    slot.active = false;slot.id = "";slot.outline.visible(false);
  });
  if (railwayTexture) railwayTexture.clear(0).commit();
  radarCells.forEach(slot => slot.element.visible(false));
  landmarks.forEach(slot => { slot.active = false;slot.element.visible(false); });
  airportClusters.forEach(cluster => {
    cluster.active = false;cluster.ring.visible(false);
    cluster.dots.forEach(dot => dot.visible(false));
  });
  placeLabel.visible(false);
  routeQueue.length = 0;
  railwayQueue.length = 0;railwayNodes.length = 0;
  railwaySeen.clear();railwayNodeCells.clear();railwayEdges.clear();
  railwayTextureDirty = false;
  transitJob = null;transitRequestInFlight = false;
  requestInFlight = false;routeInFlight = false;
  radarJob = null;radarRequestInFlight = false;radarFrameId = "";
  radarObservationEpoch = 0;radarVelocityX = 0;radarVelocityY = 0;
  nextRequestTime = 0;nextTransitRequestTime = 0;nextRadarPoll = 0;
  nightAmount = -1;nextSolarUpdate = 0;
}

function readyLocationSources(sources, revision, retries) {
  return Promise.all(sources.map(source => source.ready())).then(ready => {
    if (revision !== placeRevision) return false;
    const failed = sources.filter((source, index) => !ready[index]);
    if (!failed.length) return true;
    if (retries <= 0) throw new Error(`map tiles failed for ${PLACE_NAME}`);
    return Promise.all(failed.map(source => source.reload()))
      .then(() => readyLocationSources(sources, revision, retries - 1));
  });
}

function switchLocation(index) {
  const nextIndex = (index + PLACE_NAMES.length) % PLACE_NAMES.length;
  const nextPlace = LOCATIONS[PLACE_NAMES[nextIndex]];
  const revision = ++placeRevision;
  locationSwitching = true;
  snapshotFlights();
  placeIndex = nextIndex;PLACE_NAME = PLACE_NAMES[nextIndex];PLACE = nextPlace;
  clearLocationData();
  const longitude = PLACE.mapCenter.longitude;
  const latitude = PLACE.mapCenter.latitude;
  const zoom = PLACE.mapZoom;
  const sources = [map, darkMap, satelliteMap, nightLights];
  sources.forEach(source => source.viewport(longitude, latitude, zoom));
  readyLocationSources(sources, revision, 3).then(ready => {
    if (revision !== placeRevision) return;
    if (!ready) return;
    updatePlaceLayout();
    const cache = placeCache();
    restoreFlights(cache);
    renderAirportCounts(cache.airportCounts);
    applyTransit(transit, cache.rail, true, TRANSIT_HOLD_SECONDS);
    applyTransit(busTransit, cache.buses, false, TRANSIT_HOLD_SECONDS);
    cache.railways.forEach(item => {
      const key = `${item.kind}:${item.polyline}`;
      if (railwaySeen.has(key)) return;
      railwaySeen.add(key);railwayQueue.push(item);
    });
    // Select the new location's solar state before exposing its first frame;
    // otherwise the freshly rebaked night texture can flash for one frame.
    updateSun(clockTime, true);
    locationSwitching = false;
    nextLocationSwitch = clockTime + LOCATION_SWITCH_SECONDS;
    requestFlights();
    if (PLACE.radar !== false) requestRadar();
    if (placeHasTransit()) {
      if (cache.rail.length || cache.buses.length) {
        nextTransitRequestTime = clockTime + TRANSIT_POLL_SECONDS;
      } else {
        requestTransit();
      }
    }
  }).catch(error => {
    if (revision !== placeRevision) return;
    fx.log(`LOCATION ${PLACE_NAME} failed: ${error.message || error}`);
    locationSwitching = false;
    nextLocationSwitch = clockTime + 10;
  });
}

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
    if (!landmark.active) return;
    if (landmark.brightnessStep === step) return;
    landmark.brightnessStep = step;
    landmark.element.color(brightnessColor(landmark.color, step / 12));
  });
}

function labelText(slot) {
  const route = routeCache.get(slot.callsign);
  return route && route.expiresAt > clockTime ? route.text : "";
}

function airportCounts(payload) {
  return configuredAirports().map(airport => payload.ac.filter(row => row &&
    row.alt_baro === "ground" && Number.isFinite(row.lon) &&
    Number.isFinite(row.lat) && distanceKm(Number(row.lon), Number(row.lat),
      airport.longitude, airport.latitude) <= PLACE.airportGroundRadiusKm &&
    row.t !== "TWR" && !String(row.category || "").startsWith("C")).length);
}

function renderAirportCluster(cluster, value) {
  if (!cluster.active) return;
  const count = Math.min(Number(value || 0), cluster.dots.length);
  const growth = clamp((count - 9) / Math.max(1, cluster.dots.length - 9), 0, 1);
  const clusterRadius = AIRPORT_CLUSTER_MIN_RADIUS +
    (AIRPORT_CLUSTER_MAX_RADIUS - AIRPORT_CLUSTER_MIN_RADIUS) * Math.sqrt(growth);
  cluster.ring.scale(clusterRadius)
    .color(count > 0 ? 0xb88f32ff : 0x585858ff).visible(true);
  const goldenAngle = Math.PI * (3 - Math.sqrt(5));
  cluster.dots.forEach((dot, index) => {
    if (index >= count) { dot.visible(false); return; }
    const radius = Math.max(2, clusterRadius - 4) *
      Math.sqrt((index + 0.5) / count);
    const angle = index * goldenAngle;
    dot.position(cluster.x + Math.cos(angle) * radius,
      cluster.y + Math.sin(angle) * radius).visible(true);
  });
}

function renderAirportCounts(values) {
  airportClusters.forEach((cluster, index) =>
    renderAirportCluster(cluster, values[index]));
}

function positionLabel(slot) {
  if (!slot.label) return;
  const x = Math.round(slot.currentX + Math.max(24, slot.markerRadius * 2.2));
  const y = Math.round(slot.currentY - 10);
  if (x === slot.labelX && y === slot.labelY) return;
  slot.labelX = x;
  slot.labelY = y;
  slot.label.position(x, y);
}

function queueRoute(callsign) {
  if (!callsign || routeQueue.includes(callsign)) return;
  const cached = routeCache.get(callsign);
  if (cached && cached.expiresAt > clockTime) return;
  routeCache.delete(callsign);
  routeQueue.push(callsign);
}

function finishRouteRequest(callsign, route) {
  routeCache.set(callsign, {
    text: route || "",
    expiresAt: clockTime + ROUTE_CACHE_SECONDS
  });
  flights.forEach(slot => {
    if (slot.label && slot.active && slot.callsign === callsign) {
      slot.label.text(labelText(slot));
    }
  });
  routeInFlight = false;
  nextRouteRequestTime = clockTime + 2;
}

function placeName(airport) {
  return String(airport.municipality || airport.name ||
    airport.iata_code || airport.icao_code || "").trim().toUpperCase();
}

function isLocalAirport(airport) {
  const codes = PLACE.localAirports || configuredAirports().reduce((all, value) =>
    all.concat([value.iata, value.icao]), []);
  const iata = String(airport.iata_code || "").toUpperCase();
  const icao = String(airport.icao_code || "").toUpperCase();
  return codes.includes(iata) || codes.includes(icao);
}

function airportCoordinates(airport) {
  const latitude = Number(airport && airport.latitude);
  const longitude = Number(airport && airport.longitude);
  return Number.isFinite(latitude) && Number.isFinite(longitude) ?
    { latitude, longitude } : null;
}

function radians(value) {
  return value * Math.PI / 180;
}

function angularDistance(from, to) {
  const latitudeA = radians(from.latitude);
  const latitudeB = radians(to.latitude);
  const latitudeDelta = latitudeB - latitudeA;
  const longitudeDelta = radians(to.longitude - from.longitude);
  const haversine = Math.sin(latitudeDelta / 2) ** 2 +
    Math.cos(latitudeA) * Math.cos(latitudeB) * Math.sin(longitudeDelta / 2) ** 2;
  return 2 * Math.atan2(Math.sqrt(haversine), Math.sqrt(Math.max(0, 1 - haversine)));
}

function bearingDegrees(from, to) {
  const latitudeA = radians(from.latitude);
  const latitudeB = radians(to.latitude);
  const longitudeDelta = radians(to.longitude - from.longitude);
  const y = Math.sin(longitudeDelta) * Math.cos(latitudeB);
  const x = Math.cos(latitudeA) * Math.sin(latitudeB) -
    Math.sin(latitudeA) * Math.cos(latitudeB) * Math.cos(longitudeDelta);
  return (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;
}

function headingDifference(first, second) {
  const difference = Math.abs(first - second) % 360;
  return Math.min(difference, 360 - difference);
}

function routeMatchesAircraft(slot, origin, destination) {
  const from = airportCoordinates(origin);
  const to = airportCoordinates(destination);
  if (!slot || !from || !to) return false;
  const aircraft = { latitude: slot.latitude, longitude: slot.longitude };
  const routeDistance = angularDistance(from, to);
  if (routeDistance < 0.0001) return false;
  if (!isLocalAirport(origin) && !isLocalAirport(destination)) {
    const fromAircraft = angularDistance(from, aircraft);
    const routeBearing = radians(bearingDegrees(from, to));
    const aircraftBearing = radians(bearingDegrees(from, aircraft));
    const crossTrack = Math.abs(Math.asin(clamp(
      Math.sin(fromAircraft) * Math.sin(aircraftBearing - routeBearing), -1, 1))) * 6371;
    if (crossTrack > ROUTE_MAX_CROSS_TRACK_KM) return false;
  }
  const destinationDistance = angularDistance(aircraft, to) * 6371;
  if (destinationDistance > 20 && headingDifference(slot.headingDegrees,
    bearingDegrees(aircraft, to)) > ROUTE_MAX_HEADING_ERROR) return false;
  return true;
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
      const slot = flights.find(value => value.active && value.callsign === callsign);
      finishRouteRequest(callsign, routeMatchesAircraft(slot, origin, destination) ?
        routeDescription(origin, destination) : "");
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

function flightMotion(slot, item, sameAircraft, hasFreshPosition, positionTime) {
  let heading = Number(item.heading);
  let speed = Number(item.velocity);
  const headingMissing = !Number.isFinite(heading) || heading === 0;
  const speedMissing = !Number.isFinite(speed) || speed <= 0;

  if (!item.onGround && sameAircraft && hasFreshPosition &&
      (headingMissing || speedMissing)) {
    const elapsed = positionTime - slot.positionTime;
    const travelled = distanceKm(slot.longitude, slot.latitude,
      item.longitude, item.latitude);
    const inferredSpeed = elapsed > 0 ? travelled * 1000 / elapsed : 0;
    const usableDisplacement = elapsed > 0 &&
      elapsed <= FLIGHT_INFERENCE_MAX_SECONDS &&
      travelled >= FLIGHT_INFERENCE_MIN_DISTANCE_KM &&
      inferredSpeed <= FLIGHT_INFERENCE_MAX_SPEED;
    if (usableDisplacement) {
      if (headingMissing) {
        heading = bearingDegrees(
          { longitude: slot.longitude, latitude: slot.latitude },
          { longitude: item.longitude, latitude: item.latitude });
      }
      if (speedMissing) speed = inferredSpeed;
    }
  }

  // A repeated/stationary observation cannot produce a useful new vector.
  // Retain the last usable vector instead of returning the marker to north
  // and stopping it until another ADS-B response arrives.
  if (!Number.isFinite(heading) || heading === 0) {
    heading = sameAircraft ? slot.headingDegrees : 0;
  }
  if (!Number.isFinite(speed) || speed <= 0) {
    speed = sameAircraft ? slot.speedMetresPerSecond : 0;
  }
  return { heading, velocity: speed };
}

function setHeading(slot, heading, speed) {
  const angle = heading * Math.PI / 180;
  const trailScale = clamp(speed / 130, 0.3, 1.5) *
    (slot.aircraftKind === "fighter" ? 1.35 : 1);
  slot.headingAngle = angle;
  slot.trail.forEach((segment, index) => {
    slot.trailState[index].spacing = (index === 0 ? 34 : 25) * trailScale;
    const speedThreshold = 15 + index * 45;
    segment.visible(slot.aircraftKind !== "helicopter" && speed >= speedThreshold &&
      (!denseFlightMode || index === 0));
  });
  slot.outline.rotation(angle);
  slot.shadow.rotation(angle);
}

function positionShadow(slot) {
  const height = Math.sqrt(clamp(slot.altitudeFeet, 0, 40000) / 40000);
  const distance = SHADOW_MIN_OFFSET + height *
    (SHADOW_MAX_OFFSET - SHADOW_MIN_OFFSET);
  slot.shadow.position(slot.currentX - solarDirection.x * distance,
    slot.currentY - solarDirection.y * distance);
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
    helicopter: [AIRCRAFT_SHAPES.helicopter, 2.3],
    fighter: [AIRCRAFT_SHAPES.fighter, 2.4],
    cargo: [AIRCRAFT_SHAPES.cargo, 2.5],
    propeller: [AIRCRAFT_SHAPES.propeller, 2.4],
    small: [AIRCRAFT_SHAPES.small, 2.3],
    big: [AIRCRAFT_SHAPES.big, 2.5]
  };
  const style = styles[kind] || styles.small;
  const kindChanged = slot.aircraftKind !== kind;
  slot.aircraftKind = kind;
  slot.markerRadius = radius;
  if (kindChanged) {
    slot.outline.points(style[0]);
    slot.shadow.points(style[0]);
  }
  slot.outline.scale(radius * style[1]).color(0xffffffff);
  slot.shadow.scale(radius * style[1] * 0.94);
}

function applyFlights(values, live) {
  denseFlightMode = values.length > DENSE_FLIGHT_THRESHOLD;
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
    const sameAircraft = slot.active && slot.id === item.id;
    const positionTime = Number(item.positionTime || 0);
    const hasFreshPosition = !sameAircraft || positionTime > slot.positionTime;
    const motion = flightMotion(slot, item, sameAircraft, hasFreshPosition,
      positionTime);
    const velocity = screenVelocity({
      longitude: item.longitude,
      latitude: item.latitude,
      heading: motion.heading,
      velocity: motion.velocity
    });
    slot.id = item.id;
    slot.callsign = item.callsign || `AIRCRAFT ${index + 1}`;
    slot.onGround = Boolean(item.onGround);
    slot.altitudeFeet = item.altitudeFeet;
    slot.longitude = item.longitude;
    slot.latitude = item.latitude;
    slot.headingDegrees = motion.heading;
    slot.speedMetresPerSecond = motion.velocity;
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
    slot.shadow.visible(!slot.onGround && nightAmount === 0);
    positionShadow(slot);
    if (slot.label) slot.label.visible(!slot.onGround).text(labelText(slot));
    positionLabel(slot);
    if (!slot.onGround) queueRoute(slot.callsign);
    setHeading(slot, motion.heading, motion.velocity);
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
    slot.speedMetresPerSecond = 0;
    slot.trailInitialized = false;
    slot.marker.visible(false);
    slot.shadow.visible(false);
    slot.trail.forEach(segment => segment.visible(false));
    if (slot.label) slot.label.visible(false);
  });

}

function snapshotFlights() {
  const cache = placeCache();
  cache.flightSavedAt = Date.now();
  cache.flights = flights.filter(slot => slot.active).map(slot => {
    const point = map.unproject(slot.currentX, slot.currentY);
    return {
      id: slot.id,
      callsign: slot.callsign,
      longitude: point.longitude,
      latitude: point.latitude,
      positionTime: slot.positionTime,
      velocity: slot.speedMetresPerSecond,
      heading: slot.headingDegrees,
      aircraftKind: slot.aircraftKind,
      markerRadius: slot.markerRadius,
      altitudeFeet: slot.altitudeFeet,
      onGround: slot.onGround
    };
  });
}

function restoreFlights(cache) {
  const age = clamp((Date.now() - Number(cache.flightSavedAt || Date.now())) / 1000,
    0, 5 * 60);
  const values = cache.flights.map(item => {
    const radians = item.heading * Math.PI / 180;
    const northMetres = Math.cos(radians) * item.velocity * age;
    const eastMetres = Math.sin(radians) * item.velocity * age;
    return Object.assign({}, item, {
      longitude: item.longitude + eastMetres /
        (111320 * Math.max(0.2, Math.cos(item.latitude * Math.PI / 180))),
      latitude: item.latitude + northMetres / 111320
    });
  });
  applyFlights(values, true);
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
  if (!id || !Number.isFinite(longitude) || !Number.isFinite(latitude)) return false;
  const point = mapPoint(longitude, latitude);
  if (point.x < -100 || point.x > 2020 || point.y < -100 || point.y > 1180)
    return false;
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
  return true;
}

function receiveAis(text) {
  let message;
  try { message = JSON.parse(text); } catch (_) { return; }
  if (!message) return;
  if (message.error) { fx.log(`AISSTREAM ERROR: ${message.error}`); return; }
  const type = String(message.MessageType || "");
  aisMessageCount++;
  if (aisMessageCount === 1 || aisMessageCount % 100 === 0)
    fx.log(`AISSTREAM RX ${aisMessageCount} TYPE ${type || "UNKNOWN"}`);
  const body = message.Message && message.Message[type];
  if (!body) return;
  if (type === "ShipStaticData" || type === "StaticDataReport") {
    rememberShipDetails(message, body);
    return;
  }
  if (type === "PositionReport" || type === "StandardClassBPositionReport" ||
      type === "ExtendedClassBPositionReport") {
    rememberShipDetails(message, body);
    if (applyShipPosition(message, body)) {
      aisPositionCount++;
      if (aisPositionCount === 1 || aisPositionCount % 100 === 0)
        fx.log(`AISSTREAM VISIBLE ${aisPositionCount}`);
    }
  }
}

function connectAis() {
  if (!AIS_API_KEY || !PLACE.aisBounds || aisSocket ||
      clockTime < nextAisConnectTime) return;
  const socket = fx.net.websocket.connect(AIS_URL);
  aisSocket = socket;
  socket.onOpen(() => {
    fx.log("AISSTREAM OPEN / SUBSCRIBING");
    socket.send(JSON.stringify({
      APIKey: AIS_API_KEY,
      BoundingBoxes: [PLACE.aisBounds]
    }));
  });
  socket.onMessage(receiveAis);
  const disconnected = () => {
    if (aisSocket !== socket) return;
    aisSocket = null;nextAisConnectTime = clockTime + 20;
  };
  socket.onClose(() => { fx.log("AISSTREAM CLOSED");disconnected(); });
  socket.onError(error => {
    fx.log(`AISSTREAM SOCKET ERROR: ${String(error || "unknown")}`);
    disconnected();
  });
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
  const key = `${kind}:${segment.polyline}`;
  if (placeCache().railways.length >= MAX_RAILWAY_SEGMENTS ||
      railwaySeen.has(key)) {
    return;
  }
  railwaySeen.add(key);
  railwayQueue.push({ kind, polyline: segment.polyline });
  placeCache().railways.push({ kind, polyline: segment.polyline });
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
  if (!railwayTexture || nodes.length < 2) return;
  while (nodes.length >= 2) {
    const chunk = nodes.slice(0, 64).map(node => [node.x, node.y]);
    railwayTexture.path(chunk, 2, kind === "metro" ?
      METRO_PATH_RASTER_COLOR : TRAIN_PATH_RASTER_COLOR);
    railwayTextureDirty = true;
    if (nodes.length <= 64) return;
    nodes = nodes.slice(63);
  }
}

function processRailwayQueue() {
  if (!railwayQueue.length) {
    if (railwayTextureDirty && !transitJob) {
      railwayTexture.commit();
      railwayTextureDirty = false;
    }
    return;
  }
  const item = railwayQueue.shift();
  const nodes = [];
  decodeTransitPath(item.polyline).forEach(point => {
    const node = railwayNode(point);
    if (!nodes.length || nodes[nodes.length - 1].id !== node.id) nodes.push(node);
  });
  let run = [];
  for (let index = 1; index < nodes.length; index++) {
    const first = nodes[index - 1];
    const second = nodes[index];
    const firstLocation = map.unproject(first.x, first.y);
    const secondLocation = map.unproject(second.x, second.y);
    if (distanceKm(firstLocation.longitude, firstLocation.latitude,
        secondLocation.longitude, secondLocation.latitude) > RAILWAY_MAX_EDGE_KM) {
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
  if (METRO_MODES.has(mode)) {
    slot.marker.shape("circle", 5, 5, 2.5).color(METRO_COLOR);
  } else {
    slot.marker.shape("circle", 6, 6, 3).color(TRAIN_COLOR);
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

function correctTransitPosition(slot, point, smooth) {
  let separationKm = Infinity;
  if (slot.positionInitialized) {
    const current = map.unproject(slot.currentX, slot.currentY);
    const target = map.unproject(point.x, point.y);
    separationKm = distanceKm(current.longitude, current.latitude,
      target.longitude, target.latitude);
  }
  if (!smooth || !slot.positionInitialized ||
      separationKm > TRANSIT_SNAP_DISTANCE_KM) {
    slot.currentX = point.x;
    slot.currentY = point.y;
    slot.positionInitialized = true;
    slot.correctionX = 0;
    slot.correctionY = 0;
    slot.correctionRemaining = 0;
    return;
  }
  slot.correctionX = point.x - slot.currentX;
  slot.correctionY = point.y - slot.currentY;
  slot.correctionRemaining = TRANSIT_CORRECTION_SECONDS;
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
    correctTransitPosition(slot, point, continuing);
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

function pumpTransitRequests(job) {
  while (transitJob === job && job.active < TRANSIT_REQUEST_CONCURRENCY &&
      job.nextRegion < job.regions.length) {
    const region = job.regions[job.nextRegion++];
    const url = `${TRANSIT_URL}?zoom=${region.zoom}` +
      `&min=${region.min[0]},${region.min[1]}` +
      `&max=${region.max[0]},${region.max[1]}` +
      `&startTime=${job.start}&endTime=${job.end}&precision=${region.precision || 4}`;
    job.active++;
    fetch(url, { headers: job.headers }).then(response => {
      if (!response.ok) throw new Error(`Transitous HTTP ${response.status}`);
      return response.text();
    }).then(body => {
      if (transitJob !== job) return;
      job.tasks.push({
        body, kinds: region.kinds, scanIndex: 0, itemStart: -1,
        depth: 0, inString: false, escaped: false, opened: false
      });
      job.active--;
      job.pending--;
      if (job.pending === 0) transitRequestInFlight = false;
    })
    .catch(error => {
      fx.log(`TRANSITOUS ${error.message || error}`);
      if (transitJob !== job) return;
      job.active--;
      job.pending--;
      if (job.pending === 0) transitRequestInFlight = false;
      pumpTransitRequests(job);
    });
  }
}

function requestTransit() {
  if (!placeHasTransit() || transitRequestInFlight || transitJob) return;
  transitRequestInFlight = true;
  const now = Date.now();
  const windowSeconds = PLACE.transit.windowSeconds || TRANSIT_WINDOW_SECONDS;
  const job = {
    pending: PLACE.transit.regions.length,
    regions: PLACE.transit.regions,
    active: 0,
    nextRegion: 0,
    start: encodeURIComponent(new Date(now - windowSeconds * 1000).toISOString()),
    end: encodeURIComponent(new Date(now + windowSeconds * 1000).toISOString()),
    headers: { "User-Agent": TRANSIT_USER_AGENT },
    tasks: [], now, trains: [], metro: [], buses: []
  };
  transitJob = job;
  pumpTransitRequests(job);
}

function nextTransitJsonItem(task) {
  const budgetEnd = Math.min(task.body.length, task.scanIndex + 32768);
  for (let index = task.scanIndex; index < budgetEnd; index++) {
    const character = task.body[index];
    if (!task.opened) {
      if (/\s/.test(character)) continue;
      if (character !== "[") throw new Error("invalid Transitous response");
      task.opened = true;
      task.scanIndex = index + 1;
      continue;
    }
    if (task.itemStart < 0) {
      if (/\s/.test(character) || character === ",") {
        task.scanIndex = index + 1;
        continue;
      }
      if (character === "]") {
        task.scanIndex = index + 1;
        return { done: true };
      }
      if (character !== "{" && character !== "[") {
        throw new Error("invalid Transitous array item");
      }
      task.itemStart = index;
      task.depth = 0;
      task.inString = false;
      task.escaped = false;
    }
    if (task.inString) {
      if (task.escaped) task.escaped = false;
      else if (character === "\\") task.escaped = true;
      else if (character === "\"") task.inString = false;
    } else if (character === "\"") {
      task.inString = true;
    } else if (character === "{" || character === "[") {
      task.depth++;
    } else if (character === "}" || character === "]") {
      task.depth--;
      if (task.depth === 0) {
        const text = task.body.slice(task.itemStart, index + 1);
        task.itemStart = -1;
        task.scanIndex = index + 1;
        return { done: false, text };
      }
    }
    task.scanIndex = index + 1;
  }
  if (task.scanIndex >= task.body.length) {
    throw new Error("incomplete Transitous response");
  }
  return null;
}

function applyTransitProgress(job, final) {
  // Put metro first so a dense commuter response cannot consume every shared
  // rail marker before the subway regions arrive.
  const rail = mergeTransit(job.metro).concat(mergeTransit(job.trains));
  applyTransit(transit, rail.slice(0, MAX_RAIL_TRANSIT), true,
    TRANSIT_HOLD_SECONDS);
  const buses = mergeTransit(job.buses);
  applyTransit(busTransit, buses.slice(0, MAX_BUSES), false,
    TRANSIT_HOLD_SECONDS);
  if (!final) return;
  const cache = placeCache();
  cache.rail = rail.slice(0, MAX_RAIL_TRANSIT);
  cache.buses = buses.slice(0, MAX_BUSES);
  if (rail.length > MAX_RAIL_TRANSIT) {
    fx.log(`TRANSITOUS RAIL LIMITED ${rail.length}/${MAX_RAIL_TRANSIT}`);
  }
  if (buses.length > MAX_BUSES) {
    fx.log(`TRANSITOUS BUSES LIMITED ${buses.length}/${MAX_BUSES}`);
  }
}

function processTransitJob() {
  if (!transitJob) return;
  const job = transitJob;
  if (job.tasks.length) {
    const task = job.tasks[0];
    try {
      const next = nextTransitJsonItem(task);
      if (!next) return;
      if (next.done) {
        job.tasks.shift();
        applyTransitProgress(job, false);
        pumpTransitRequests(job);
        return;
      }
      const item = JSON.parse(next.text);
      task.kinds.forEach(kind => {
        if (kind === "train") {
          job.trains.push(...normalizeTransit([item], job.now, TRANSIT_MODES, "train"));
        } else if (kind === "metro") {
          job.metro.push(...normalizeTransit([item], job.now, METRO_MODES, "metro"));
        } else if (kind === "bus") {
          job.buses.push(...normalizeTransit([item], job.now, BUS_MODES));
        }
      });
    } catch (error) {
      fx.log(`TRANSITOUS ${error.message || error}`);
      job.tasks.shift();
      pumpTransitRequests(job);
    }
    return;
  }
  if (job.pending === 0) {
    applyTransitProgress(job, true);
    transitJob = null;
    nextTransitRequestTime = clockTime + TRANSIT_POLL_SECONDS;
  }
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
    correctTransitPosition(slot, point, true);
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

function updateSun(now, force) {
  if (!force && now < nextSolarUpdate) return;
  nextSolarUpdate = now + SUN_UPDATE_SECONDS;
  const sun = fx.geo.sunPosition(new Date(), PLACE.mapCenter.latitude,
    PLACE.mapCenter.longitude);
  const amount = sun.elevationDegrees < NIGHT_SWITCH_ELEVATION ? 1 : 0;
  if (amount !== nightAmount) {
    nightAmount = amount;
    if (nightAmount === 1) {
      dayView.hide();
      nightView.show().blend(false).opacity(1);
    } else {
      nightView.hide();
      dayView.show().blend(false).opacity(1);
    }
    flights.forEach(slot => {
      slot.shadow.visible(slot.active && !slot.onGround && nightAmount === 0);
    });
    busTransit.forEach(slot => {
      slot.marker.color(nightAmount === 0 ? BUS_DAY_COLOR : BUS_NIGHT_COLOR);
    });
  }
  // Screen north points upward. Only geographic sun azimuth controls the
  // direction; aircraft altitude alone controls the shadow distance.
  solarDirection = { x: sun.east, y: -sun.north };
}

function radarNumber(value) {
  return Array.isArray(value) ? Number(value[0]) : Number(value);
}

function radarProjection(decoded) {
  if (decoded.radarProjection) return decoded.radarProjection;
  const where = decoded.attributes["/where"];
  const parameters = {};
  String(where.projdef || "").trim().split(/\s+/).forEach(part => {
    const match = /^\+([^=]+)=(.+)$/.exec(part);
    if (match) parameters[match[1]] = match[2];
  });
  if (parameters.proj !== "stere") {
    throw new Error(`unsupported radar projection ${parameters.proj || "missing"}`);
  }
  const projection = fx.geo.obliqueStereographic({
    latitudeOrigin: Number(parameters.lat_0),
    longitudeOrigin: Number(parameters.lon_0),
    semiMajor: 6378137,
    inverseFlattening: 298.257223563,
    scaleFactor: Number(parameters.k_0 || parameters.k || 1)
  });
  const upperLeft = projection.forward(radarNumber(where.UL_lon),
    radarNumber(where.UL_lat));
  decoded.radarProjection = {
    projection,
    upperLeft,
    xScale: radarNumber(where.xscale),
    yScale: radarNumber(where.yscale)
  };
  return decoded.radarProjection;
}

function radarPosition(column, row, decoded) {
  const grid = radarProjection(decoded);
  const location = grid.projection.inverse(
    grid.upperLeft.x + column * grid.xScale,
    grid.upperLeft.y - row * grid.yScale);
  return mapPoint(location.longitude, location.latitude);
}

function makeComponentScanner(decoded) {
  return {
    decoded,
    cursor: 0,
    visited: new Uint8Array(decoded.data.length),
    stack: [],
    cells: null,
    intensity: 0,
    sumX: 0,
    sumY: 0,
    components: [],
    done: false
  };
}

function finishScannedComponent(scanner) {
  if (scanner.cells && scanner.cells.length >= 6) {
    scanner.components.push({
      cells: scanner.cells,
      intensity: scanner.intensity / scanner.cells.length,
      column: scanner.sumX / scanner.cells.length,
      row: scanner.sumY / scanner.cells.length
    });
  }
  scanner.cells = null;
  scanner.intensity = 0;
  scanner.sumX = 0;
  scanner.sumY = 0;
}

function scanComponents(scanner, budget) {
  const values = scanner.decoded.data;
  const rows = scanner.decoded.shape[0];
  const columns = scanner.decoded.shape[1];
  let work = 0;
  while (!scanner.done && work < budget) {
    if (!scanner.stack.length) {
      finishScannedComponent(scanner);
      let origin = -1;
      while (scanner.cursor < values.length && work < budget) {
        const index = scanner.cursor++;
        work++;
        if (!scanner.visited[index] && values[index] >= RADAR.threshold &&
            values[index] !== RADAR.nodata) {
          origin = index;
          break;
        }
      }
      if (origin < 0) {
        if (scanner.cursor >= values.length) {
          scanner.done = true;
          scanner.components.sort((a, b) => b.cells.length - a.cells.length);
        }
        continue;
      }
      scanner.visited[origin] = 1;
      scanner.stack.push(origin);
      scanner.cells = [];
    }
    const index = scanner.stack.pop();
    const y = Math.floor(index / columns);
    const x = index - y * columns;
    scanner.cells.push([x, y]);
    scanner.intensity += values[index];
    scanner.sumX += x;
    scanner.sumY += y;
    work++;
    for (let dy = -1; dy <= 1; dy++) {
      for (let dx = -1; dx <= 1; dx++) {
        const nx = x + dx;
        const ny = y + dy;
        if ((dx === 0 && dy === 0) || nx < 0 || nx >= columns ||
            ny < 0 || ny >= rows) continue;
        const next = ny * columns + nx;
        if (!scanner.visited[next] && values[next] >= RADAR.threshold &&
            values[next] !== RADAR.nodata) {
          scanner.visited[next] = 1;
          scanner.stack.push(next);
        }
      }
    }
  }
}

function motionCandidates(step, centerX, centerY, radius) {
  const candidates = [];
  const cx = centerX || 0;
  const cy = centerY || 0;
  const limit = radius === undefined ? RADAR.motionRange : radius;
  for (let y = cy - limit; y <= cy + limit; y += step) {
    for (let x = cx - limit; x <= cx + limit; x += step) {
      if (Math.abs(x) <= RADAR.motionRange && Math.abs(y) <= RADAR.motionRange)
        candidates.push([x, y]);
    }
  }
  return candidates;
}

function scoreRadarMotion(previous, current, dx, dy, sample) {
  const rows = current.shape[0];
  const columns = current.shape[1];
  const step = sample;
  let overlap = 0;
  let union = 0;
  for (let y = RADAR.motionRange; y < rows - RADAR.motionRange; y += step) {
    for (let x = RADAR.motionRange; x < columns - RADAR.motionRange; x += step) {
      const a = current.data[y * columns + x];
      const b = previous.data[(y - dy) * columns + x - dx];
      const wetA = a >= RADAR.threshold && a !== RADAR.nodata;
      const wetB = b >= RADAR.threshold && b !== RADAR.nodata;
      if (wetA || wetB) union++;
      if (wetA && wetB) overlap++;
    }
  }
  return union >= 20 ? overlap / union : -1;
}

function radarCellShape(cell, decoded) {
  const column = (cell[0] + 0.5) * RADAR.stride;
  const row = (cell[1] + 0.5) * RADAR.stride;
  const center = radarPosition(column, row, decoded);
  const right = radarPosition(column + RADAR.stride, row, decoded);
  const down = radarPosition(column, row + RADAR.stride, decoded);
  const ux = (right.x - center.x) * 0.54;
  const uy = (right.y - center.y) * 0.54;
  const vx = (down.x - center.x) * 0.54;
  const vy = (down.y - center.y) * 0.54;
  return {
    center,
    points: [
      [-ux - vx, -uy - vy],
      [ux - vx, uy - vy],
      [ux + vx, uy + vy],
      [-ux + vx, -uy + vy]
    ],
    intensity: decoded.data[cell[1] * decoded.shape[1] + cell[0]]
  };
}

function radarColor(raw) {
  // DMI composite values use gain 0.5 and offset -32 dBZ. These broad bands
  // deliberately echo the public DMI legend without pretending to be an
  // exact copy of the website's separately generated five-minute product.
  if (raw < 84) return fx.rgba(116, 231, 222, 118);
  if (raw < 104) return fx.rgba(28, 207, 188, 142);
  if (raw < 124) return fx.rgba(38, 139, 221, 164);
  if (raw < 144) return fx.rgba(255, 210, 0, 188);
  if (raw < 154) return fx.rgba(255, 128, 70, 198);
  if (raw < 164) return fx.rgba(255, 82, 82, 208);
  if (raw < 174) return fx.rgba(211, 28, 31, 218);
  return fx.rgba(112, 0, 40, 228);
}

function beginRadarJob(previous, current, previousFeature, currentFeature) {
  radarJob = {
    phase: "scan-previous",
    previous,
    current,
    previousFeature,
    currentFeature,
    previousScanner: makeComponentScanner(previous),
    currentScanner: makeComponentScanner(current),
    motions: motionCandidates(2, 0, 0, RADAR.motionRange),
    motionIndex: 0,
    motionPass: "coarse",
    bestMotion: [0, 0],
    bestScore: -1,
    contourIndex: 0,
    contourCellIndex: 0,
    renderCells: []
  };
}

function finishRadarJob(job) {
  const frameSeconds = Math.max(1, (Date.parse(job.currentFeature.properties.datetime) -
    Date.parse(job.previousFeature.properties.datetime)) / 1000);
  const reference = radarPosition(job.current.shape[1] * RADAR.stride * 0.5,
    job.current.shape[0] * RADAR.stride * 0.5, job.current);
  const moved = radarPosition(
    (job.current.shape[1] * 0.5 + job.bestMotion[0]) * RADAR.stride,
    (job.current.shape[0] * 0.5 + job.bestMotion[1]) * RADAR.stride,
    job.current);
  radarVelocityX = (moved.x - reference.x) / frameSeconds;
  radarVelocityY = (moved.y - reference.y) / frameSeconds;
  radarObservationEpoch = Date.parse(job.currentFeature.properties.datetime) / 1000;
  job.renderCells.sort((a, b) => a.intensity - b.intensity);
  job.renderCells.forEach((shape, index) => {
    const slot = radarCells[index];
    slot.centerX = shape.center.x;
    slot.centerY = shape.center.y;
    slot.element.points(shape.points).position(slot.centerX, slot.centerY)
      .color(radarColor(shape.intensity)).visible(true);
  });
  for (let index = job.renderCells.length; index < radarCells.length; index++) {
    radarCells[index].element.visible(false);
  }
  radarFrameId = job.currentFeature.id;
  radarJob = null;
}

function processRadarJob() {
  const job = radarJob;
  if (!job) return;
  if (job.phase === "scan-previous") {
    scanComponents(job.previousScanner, RADAR.scanBudget);
    if (job.previousScanner.done) job.phase = "scan-current";
    return;
  }
  if (job.phase === "scan-current") {
    scanComponents(job.currentScanner, RADAR.scanBudget);
    if (job.currentScanner.done) job.phase = "motion";
    return;
  }
  if (job.phase === "motion") {
    const motion = job.motions[job.motionIndex++];
    const sample = job.motionPass === "coarse" ? RADAR.motionSample : 4;
    const score = scoreRadarMotion(job.previous, job.current,
      motion[0], motion[1], sample);
    if (score > job.bestScore) {
      job.bestScore = score;
      job.bestMotion = motion;
    }
    if (job.motionIndex >= job.motions.length) {
      if (job.motionPass === "coarse") {
        job.motionPass = "fine";
        job.motions = motionCandidates(1, job.bestMotion[0],
          job.bestMotion[1], 2);
        job.motionIndex = 0;
        job.bestScore = -1;
      } else {
        job.phase = "contours";
      }
    }
    return;
  }
  const candidates = job.currentScanner.components;
  let work = 0;
  while (job.contourIndex < candidates.length &&
      job.renderCells.length < RADAR.renderCells && work < RADAR.scanBudget) {
    const component = candidates[job.contourIndex++];
    const componentCenter = radarPosition(component.column * RADAR.stride,
      component.row * RADAR.stride, job.current);
    if (componentCenter.x < -600 || componentCenter.x > fx.width + 600 ||
        componentCenter.y < -600 || componentCenter.y > fx.height + 600) {
      continue;
    }
    job.contourIndex--;
    while (job.contourCellIndex < component.cells.length &&
        job.renderCells.length < RADAR.renderCells && work < RADAR.scanBudget) {
      const shape = radarCellShape(component.cells[job.contourCellIndex++],
        job.current);
      work++;
      if (shape.center.x >= -100 && shape.center.x <= fx.width + 100 &&
          shape.center.y >= -100 && shape.center.y <= fx.height + 100) {
        job.renderCells.push(shape);
      }
    }
    if (job.contourCellIndex >= component.cells.length) {
      job.contourIndex++;
      job.contourCellIndex = 0;
    }
  }
  if (job.contourIndex >= candidates.length ||
      job.renderCells.length >= RADAR.renderCells) finishRadarJob(job);
}

function radarBytes(feature) {
  const url = feature.asset.data.href;
  const cached = fx.cache.read("radar", url, 7 * 86400);
  if (cached instanceof ArrayBuffer) return Promise.resolve(cached);
  return fetch(url).then(response => {
    if (!response.ok) throw new Error(`DMI radar HTTP ${response.status}`);
    return response.arrayBuffer();
  }).then(buffer => {
    fx.cache.write("radar", url, buffer);
    return buffer;
  });
}

function decodeRadar(buffer) {
  return fx.data.decode(buffer, {
    format: "hdf5",
    dataset: RADAR.dataset,
    stride: [RADAR.stride, RADAR.stride],
    attributes: ["/what", "/where", "/dataset1/data1"]
  });
}

function requestRadar() {
  if (PLACE.radar === false || radarRequestInFlight || radarJob) return;
  const revision = placeRevision;
  radarRequestInFlight = true;
  fetch(RADAR.listUrl).then(response => {
    if (!response.ok) throw new Error(`DMI list HTTP ${response.status}`);
    return response.json();
  }).then(payload => {
    if (revision !== placeRevision) return null;
    const features = (payload.features || []).filter(feature =>
      feature && feature.asset && feature.asset.data && feature.properties);
    if (features.length < 2 || features[0].id === radarFrameId) return null;
    return Promise.all([radarBytes(features[1]), radarBytes(features[0])])
      .then(buffers => {
        if (revision !== placeRevision) return;
        beginRadarJob(decodeRadar(buffers[0]),
          decodeRadar(buffers[1]), features[1], features[0]);
      });
  }).catch(error => fx.log("RADAR update failed: " +
    String(error && error.message || error))).then(() => {
    if (revision !== placeRevision) return;
    radarRequestInFlight = false;
    nextRadarPoll = clockTime + RADAR.pollSeconds;
  });
}

function positionRadar() {
  if (!radarObservationEpoch) return;
  // DMI commonly publishes a completed composite 10-20 minutes after its
  // observation time. Keep extrapolating after it arrives instead of landing
  // immediately on the old ten-minute cap and freezing until the next file.
  const age = clamp(Date.now() / 1000 - radarObservationEpoch, 0,
    RADAR.motionMaxSeconds);
  const dx = radarVelocityX * age;
  const dy = radarVelocityY * age;
  radarCells.forEach(slot =>
    slot.element.position(slot.centerX + dx, slot.centerY + dy));
}

function requestFlights() {
  if (requestInFlight) return;
  const revision = placeRevision;
  const url = flightDataUrl();
  requestInFlight = true;
  fetch(url)
    .then(response => {
      if (!response.ok) throw new Error(`flight request failed: HTTP ${response.status}`);
      return response.json();
    })
    .then(payload => {
      if (revision !== placeRevision) return;
      const airborne = normalizeFlights(payload);
      const counts = airportCounts(payload);
      placeCache().airportCounts = counts;
      renderAirportCounts(counts);
      applyFlights(airborne, true);
      requestInFlight = false;
      nextRequestTime = clockTime + POLL_SECONDS;
    })
    .catch(() => {
      if (revision !== placeRevision) return;
      requestInFlight = false;
      nextRequestTime = clockTime + POLL_SECONDS;
    });
}

updatePlaceLayout();
requestRadar();
requestFlights();

function update(time, delta) {
  clockTime = time;
  if (!initialAssetsReady) {
    const startup = startupAssets.update(time);
    if (!startup.ready) {
      if (startup.sourcesReady) {
        // Bake the geographically correct opaque background behind the
        // initial loading cover. Later viewport changes use atomic handoff.
        updateSun(time, true);
      } else {
        nightView.hide();dayView.hide();
      }
      return;
    }
    initialAssetsReady = true;
  }
  scene.show();
  if (locationSwitching) return;
  if (AUTO_SWITCH_LOCATIONS && !rotationStarted) {
    rotationStarted = true;
    nextLocationSwitch = time + LOCATION_SWITCH_SECONDS;
  }
  if (AUTO_SWITCH_LOCATIONS && time >= nextLocationSwitch) {
    switchLocation(placeIndex + 1);
    return;
  }
  updateSun(time);
  processRadarJob();
  positionRadar();
  if (!radarRequestInFlight && !radarJob && time >= nextRadarPoll)
    requestRadar();
  updateLandmarks(time);
  processTransitJob();
  processRailwayQueue();
  if (!requestInFlight && time >= nextRequestTime) requestFlights();
  if (placeHasTransit() && !transitRequestInFlight && !transitJob &&
      time >= nextTransitRequestTime) {
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
