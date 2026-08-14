export function recentPeerIds(stored, fallback, limit = 6) {
  let values = [];
  try {
    const parsed = JSON.parse(stored || "[]");
    if (Array.isArray(parsed)) values = parsed;
  } catch {}
  if (fallback) values.push(fallback);
  return [...new Set(values.map((value) => String(value).trim()).filter(Boolean))].slice(0, limit);
}

export function rememberPeerId(stored, peerId, limit = 6) {
  const selected = String(peerId || "").trim();
  return [selected, ...recentPeerIds(stored, "", limit)]
    .filter(Boolean)
    .filter((value, index, values) => values.indexOf(value) === index)
    .slice(0, limit);
}

export async function uploadAssetBatch(files, uploadOne) {
  const results = [];
  for (const file of files) {
    try {
      await uploadOne(file);
      results.push({ file, ok: true });
    } catch (error) {
      results.push({ file, ok: false, error });
    }
  }
  return {
    results,
    uploaded: results.filter((result) => result.ok),
    failed: results.filter((result) => !result.ok)
  };
}
