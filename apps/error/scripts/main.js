// Firmware-owned fallback shown after an uploaded project fails to load or
// throws during its activation health check.
fx.configure({
  outputWidth: 0,
  outputHeight: 0,
  pixelDensity: 1,
  targetFps: 10,
  debugBar: false
});

fx.rect(960, 540, 1920, 1080, 0x000000ff);
fx.text("PROJECT ERROR", 90, 72, 42, 0xffffffff);
fx.text("Fix the JavaScript and choose Save & Run again.", 90, 142, 22, 0xaaaaaaff);

const raw = fx.env("MICROFX_ERROR_DETAIL", "The renderer rejected this project.");
const words = raw.replace(/\s+/g, " ").trim().split(" ");
const lines = [];
let line = "";
for (const word of words) {
  const next = line ? line + " " + word : word;
  if (next.length > 92 && line) {
    lines.push(line);
    line = word;
  } else {
    line = next;
  }
  if (lines.length === 9) break;
}
if (line && lines.length < 10) lines.push(line);
lines.forEach((value, index) => fx.text(value, 90, 240 + index * 60, 25, 0xffffffff));

function update(time, delta) {
  void time;
  void delta;
}
