function normalize(input) {
  const records = Array.isArray(input.records) ? input.records.slice(0, 24).reverse() : [];
  return {
    updated: records.length ? records[records.length - 1].HourUTC : "",
    source: "energinet", currency: "DKK/kWh", region: "DK2",
    prices: records.map(record => Number(record.SpotPriceDKK || 0) / 1000)
  };
}
