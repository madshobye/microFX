function normalize(input) {
  const rows = Array.isArray(input.states) ? input.states : [];
  const flights = rows.filter(row => Array.isArray(row) && Number.isFinite(row[5]) && Number.isFinite(row[6]))
    .slice(0, 8).map((row, index) => ({
      callsign: String(row[1] || row[0] || `CPH${index + 1}`).trim(),
      longitude: row[5], latitude: row[6], altitude: Number(row[7] || 0),
      heading: Number.isFinite(row[10]) ? row[10] : 0,
      speed: Math.max(20, Math.min(120, Number(row[9] || 50) * 0.55)),
      phase: ((Number(row[5]) - 11.4) / 1.9) * Math.PI * 2
    }));
  return { updated: Number(input.time || 0), source: "opensky", flights };
}
