fx.configure({
  targetFps: 60,
  pixelDensity: 1,
  dithering: false,
  debugBar: true,
  debugBarStyle: "compact"
});

const PLACE = {
  name: "BORNHOLM",
  longitude: 14.91,
  latitude: 55.32,
  zoom: 8.7
};
const RADAR = {
  label: "DMI RADAR / 18:20",
  url: "https://opendataapi.dmi.dk/v1/radardata/download/" +
    "dk.com.202608151620.500_max.h5",
  dataset: "/dataset1/data1/data",
  stride: 4,
  threshold: 72,
  nodata: 255
};
const BASE_MODE = "day";

const scene = fx.scenes.add(fx.scene({ name: "radar-shapes" }));
const map = scene.add(fx.tileMap({
  source: {
    url: "https://server.arcgisonline.com/ArcGIS/rest/services/" +
      "World_Imagery/MapServer/tile/{z}/{y}/{x}",
    tileSize: 256,
    maxZoom: 20,
    attribution: "Imagery © Esri, Maxar, Earthstar Geographics"
  },
  center: [PLACE.longitude, PLACE.latitude],
  zoom: PLACE.zoom,
  cacheDays: 30,
  filter: { grayscale: 0, contrast: 1, brightness: 1 }
}));

scene.add(fx.text("RAIN SHAPES / " + PLACE.name, 48, 56, 34, 0xe2e7e8ff)
  .antialias(false));
const status = scene.add(fx.text("LOADING " + RADAR.label, 50, 102, 18,
  0xaeb7baff).antialias(false));
scene.add(fx.text("DARK BY DAY / LIGHT BY NIGHT", 50, 132, 13, 0x879195ff)
  .antialias(false));
scene.add(fx.text("DAY © ESRI / RADAR © DMI", 1600, 1043, 12, 0x687377ff)
  .antialias(false));

const weather = [];

function number(value) {
  return Array.isArray(value) ? Number(value[0]) : Number(value);
}

function radarPosition(column, row, columns, rows, where) {
  const u = column / (columns - 1);
  const v = row / (rows - 1);
  const leftLongitude = number(where.UL_lon) +
    (number(where.LL_lon) - number(where.UL_lon)) * v;
  const rightLongitude = number(where.UR_lon) +
    (number(where.LR_lon) - number(where.UR_lon)) * v;
  const longitude = leftLongitude + (rightLongitude - leftLongitude) * u;
  const leftLatitude = number(where.UL_lat) +
    (number(where.LL_lat) - number(where.UL_lat)) * v;
  const rightLatitude = number(where.UR_lat) +
    (number(where.LR_lat) - number(where.UR_lat)) * v;
  const latitude = leftLatitude + (rightLatitude - leftLatitude) * u;
  return map.project(longitude, latitude);
}

function components(decoded) {
  const rows = decoded.shape[0];
  const columns = decoded.shape[1];
  const values = decoded.data;
  const visited = new Uint8Array(values.length);
  const result = [];
  const stack = [];

  for (let row = 0; row < rows; row++) {
    for (let column = 0; column < columns; column++) {
      const origin = row * columns + column;
      const value = values[origin];
      if (visited[origin] || value < RADAR.threshold || value === RADAR.nodata)
        continue;
      visited[origin] = 1;
      stack.push(origin);
      const cells = [];
      let intensity = 0;
      while (stack.length) {
        const index = stack.pop();
        const y = Math.floor(index / columns);
        const x = index - y * columns;
        cells.push([x, y]);
        intensity += values[index];
        for (let dy = -1; dy <= 1; dy++) {
          for (let dx = -1; dx <= 1; dx++) {
            const nx = x + dx;
            const ny = y + dy;
            if ((dx === 0 && dy === 0) || nx < 0 || nx >= columns ||
                ny < 0 || ny >= rows) continue;
            const next = ny * columns + nx;
            const nextValue = values[next];
            if (!visited[next] && nextValue >= RADAR.threshold &&
                nextValue !== RADAR.nodata) {
              visited[next] = 1;
              stack.push(next);
            }
          }
        }
      }
      if (cells.length >= 6)
        result.push({ cells, intensity: intensity / cells.length });
    }
  }
  return result.sort((a, b) => b.cells.length - a.cells.length);
}

function smoothContour(component, decoded, where) {
  const sourceRows = decoded.sourceShape[0];
  const sourceColumns = decoded.sourceShape[1];
  const points = component.cells.map(cell => radarPosition(
    cell[0] * RADAR.stride,
    cell[1] * RADAR.stride,
    sourceColumns,
    sourceRows,
    where));
  const center = points.reduce((sum, point) => ({
    x: sum.x + point.x,
    y: sum.y + point.y
  }), { x: 0, y: 0 });
  center.x /= points.length;
  center.y /= points.length;

  const bins = 32;
  let radii = Array(bins).fill(0);
  points.forEach(point => {
    const dx = point.x - center.x;
    const dy = point.y - center.y;
    const angle = Math.atan2(dy, dx) + Math.PI;
    const bin = Math.min(bins - 1, Math.floor(angle / (Math.PI * 2) * bins));
    radii[bin] = Math.max(radii[bin], Math.sqrt(dx * dx + dy * dy));
  });
  for (let pass = 0; pass < 4; pass++) {
    radii = radii.map((radius, index) => {
      const before = radii[(index + bins - 1) % bins];
      const after = radii[(index + 1) % bins];
      if (radius === 0) return (before + after) * 0.5;
      return before * 0.22 + radius * 0.56 + after * 0.22;
    });
  }
  const contour = expansion => radii.map((radius, index) => {
    const angle = index / bins * Math.PI * 2 - Math.PI;
    const distance = Math.max(8, radius + expansion);
    return [Math.cos(angle) * distance, Math.sin(angle) * distance];
  });
  return { center, inner: contour(2), outer: contour(16) };
}

function addWeather(decoded) {
  const where = decoded.attributes["/where"];
  const candidates = components(decoded);
  let visible = 0;
  for (const candidate of candidates) {
    if (visible >= 14) break;
    const shape = smoothContour(candidate, decoded, where);
    if (shape.center.x < -300 || shape.center.x > fx.width + 300 ||
        shape.center.y < -300 || shape.center.y > fx.height + 300) continue;
    const strength = Math.max(0, Math.min(1,
      (candidate.intensity - RADAR.threshold) / 48));
    const colors = BASE_MODE === "night" ? {
      outer: fx.rgba(190, 205, 214, 50 + strength * 28),
      inner: fx.rgba(220, 229, 232, 82 + strength * 58)
    } : {
      outer: fx.rgba(24, 33, 42, 46 + strength * 32),
      inner: fx.rgba(13, 21, 29, 74 + strength * 68)
    };
    const outer = fx.polygon(shape.outer, shape.center.x,
      shape.center.y, 1, colors.outer);
    const inner = fx.polygon(shape.inner, shape.center.x,
      shape.center.y, 1, colors.inner);
    const group = scene.add(fx.group(outer, inner));
    weather.push({
      group,
      x: shape.center.x,
      y: shape.center.y,
      phase: visible * 0.71
    });
    visible++;
  }
  status.text(RADAR.label + " / " + visible + " MOVING REGIONS");
}

function loadRadar() {
  const cached = fx.cache.read("radar", RADAR.url, 3650 * 86400);
  const bytes = cached instanceof ArrayBuffer ? Promise.resolve(cached) :
    fetch(RADAR.url).then(response => {
      if (!response.ok) throw new Error("DMI radar HTTP " + response.status);
      return response.arrayBuffer().then(buffer => {
        fx.cache.write("radar", RADAR.url, buffer);
        return buffer;
      });
    });
  bytes.then(buffer => fx.data.decode(buffer, {
    format: "hdf5",
    dataset: RADAR.dataset,
    stride: [RADAR.stride, RADAR.stride],
    attributes: ["/what", "/where", "/dataset1/data1"]
  })).then(addWeather).catch(error => {
    status.text("RADAR ERROR / " + String(error && error.message || error));
  });
}

loadRadar();

function update(time) {
  scene.show();
  weather.forEach(cloud => {
    const driftX = Math.cos(time * 0.035 + cloud.phase) * 9;
    const driftY = Math.sin(time * 0.027 + cloud.phase) * 5;
    cloud.group.position(driftX, driftY);
  });
}
