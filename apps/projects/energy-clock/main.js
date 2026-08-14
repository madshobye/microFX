fx.configure({ targetFps: 30, pixelDensity: 1, debugBar: 10 });

const DATA_URL = "https://api.energidataservice.dk/dataset/DayAheadPrices" +
  "?start=now&end=now%2BP1D&filter=%7B%22PriceArea%22:%5B%22DK2%22%5D%7D" +
  "&sort=TimeUTC%20asc&limit=96";
const fallback = fx.data("prices.json", { prices: [] });
const scene = fx.scenes.add(fx.scene({ name: "energy-clock" }));

scene.add(fx.background(0x0a1c20ff, 0x040809ff));
scene.add(fx.text("ENERGY / NEXT 24 HOURS", 64, 52, 34, 0xb9fff4ff));
const status = scene.add(fx.text("OFFLINE SNAPSHOT", 1570, 58, 18, 0x7f9da0ff));

// Fixed-height segments let a late network response change the graph without
// destroying or allocating GPU objects. Twelve segments per hour stay well
// inside the retained quad budget.
const segmentCount = 12;
const columns = Array.from({ length: 24 }, (_, hour) =>
  Array.from({ length: segmentCount }, (_, segment) =>
    scene.add(fx.rect(82 + hour * 73, 862 - segment * 47, 48, 38, 0x58e3b7ff))));

scene.add(fx.text("LOW", 80, 930, 18, 0x58e3b7ff));
scene.add(fx.text("HIGH", 1710, 930, 18, 0xff685cff));
const marker = scene.add(fx.rect(82, 900, 48, 5, 0xffffffff));

function applyPrices(values, live) {
  const prices = Array.isArray(values) ? values : [];
  columns.forEach((segments, hour) => {
    const price = Math.max(0, Number(prices[hour] || 0));
    const visibleSegments = Math.min(segmentCount, Math.ceil(price * segmentCount));
    const color = price > 0.7 ? 0xff685cff : 0x58e3b7ff;
    segments.forEach((segment, index) =>
      segment.color(color).visible(index < visibleSegments));
  });
  status.text(live ? "LIVE / ENERGINET" : "OFFLINE SNAPSHOT")
    .color(live ? 0x58e3b7ff : 0x7f9da0ff);
}

function normalizePrices(payload) {
  if (!payload || !Array.isArray(payload.records)) throw new Error("missing price records");
  const quarters = payload.records.slice(0, 96);
  return Array.from({ length: 24 }, (_, hour) => {
    const records = quarters.slice(hour * 4, hour * 4 + 4);
    if (!records.length) return 0;
    return records.reduce((sum, record) =>
      sum + Number(record.DayAheadPriceDKK || 0) / 1000, 0) / records.length;
  });
}

applyPrices(fallback.prices, false);
fetch(DATA_URL)
  .then(response => {
    if (!response.ok) throw new Error(`price request failed: HTTP ${response.status}`);
    return response.json();
  })
  .then(payload => applyPrices(normalizePrices(payload), true))
  .catch(() => applyPrices(fallback.prices, false));

function update() {
  scene.show();
  // This is a clock, not an animation timer: follow synchronized wall time.
  const now = new Date();
  const hour = now.getHours() + now.getMinutes() / 60;
  marker.position(82 + hour * 73, 900);
}
