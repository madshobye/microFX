import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const root = new URL("../buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/data-adapters/", import.meta.url);

async function adapter(name, input) {
  const source = await readFile(new URL(`${name}.js`, root), "utf8");
  const context = vm.createContext({ Math, Number, String, Array });
  vm.runInContext(source, context, { filename: `${name}.js` });
  return JSON.parse(JSON.stringify(context.normalize(input)));
}

test("flight adapter bounds and normalizes OpenSky vectors", async () => {
  const states = Array.from({ length: 10 }, (_, index) => [
    `icao${index}`, index === 0 ? " SK123  " : null, "Denmark", 0, 0,
    12.5 + index * 0.01, 55.6 + index * 0.01, 1000 + index, false, 90 + index,
    180 + index
  ]);
  states.push(["invalid", "NONE", "Denmark", 0, 0, null, null]);
  const result = await adapter("flight", { time: 1234, states });
  assert.equal(result.updated, 1234);
  assert.equal(result.source, "opensky");
  assert.equal(result.flights.length, 8);
  assert.equal(result.flights[0].callsign, "SK123");
  assert.equal(result.flights[1].callsign, "icao1");
  assert.equal(result.flights[0].heading, 180);
  assert.ok(result.flights.every(item => Number.isFinite(item.phase) && item.speed >= 20));
});

test("energy adapter converts hourly DKK/MWh to DKK/kWh in chronological order", async () => {
  const result = await adapter("energy", { records: [
    { HourUTC: "2026-08-14T02:00:00", SpotPriceDKK: 800 },
    { HourUTC: "2026-08-14T01:00:00", SpotPriceDKK: 250 }
  ] });
  assert.deepEqual(result.prices, [0.25, 0.8]);
  assert.equal(result.region, "DK2");
  assert.equal(result.currency, "DKK/kWh");
});
