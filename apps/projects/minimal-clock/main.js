fx.configure({ targetFps: 30, pixelDensity: 1, debugBar: false });
const scene = fx.scenes.add(fx.scene({ name: "minimal-clock" }));
scene.add(fx.background(0x101c2bff, 0x03070cff));
const timeText = scene.add(fx.text("00:00:00", 590, 430, 112, 0xe9f5ffff));
scene.add(fx.text("MICROFX / AMBIENT TIME", 705, 585, 22, 0x6f9ebaff));
const pulse = scene.add(fx.circle(960, 690, 6, 0x64e7ffff));
let previousSecond = -1;
function pad(value) { return String(value).padStart(2, "0"); }
function update(time) {
  const seconds = Math.floor(time) % 86400;
  if (seconds !== previousSecond) {
    previousSecond = seconds;
    timeText.text(pad(Math.floor(seconds / 3600)) + ":" +
                  pad(Math.floor(seconds / 60) % 60) + ":" + pad(seconds % 60));
  }
  pulse.position(960 + Math.sin(time) * 14, 690);
}
