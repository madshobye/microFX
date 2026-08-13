// Firmware onboarding application. It is an ordinary engine JavaScript scene
// and remains portable; the platform supervisor only supplies environment data.
fx.configure({
  outputWidth: 0,
  outputHeight: 0,
  pixelDensity: 1.0,
  minimumPixelDensity: 0.5,
  targetFps: 30,
  debugBar: false,
  durationSeconds: 40
});

const peerId = fx.env("MICROFX_PEER_ID", fx.product.defaultPeerId);
const apSsid = fx.env("MICROFX_AP_SSID", fx.product.defaultSetupSsid);
const apPassword = fx.env("MICROFX_AP_PASSWORD", fx.product.defaultSetupPassword);
const portalUrl = "http://10.42.0.1";

fx.gradientRect(960, 540, 1920, 1080, 0x101b3dff, 0x050916ff);
fx.gradientRect(960, 82, 1920, 164, 0x162b5dff, 0x0b1532ff);
fx.text(fx.product.name, 92, 46, 52, 0xffdf62ff);
fx.text("SETUP & REMOTE EDITING", 94, 112, 22, 0x72e6ffff);

// Version 2-L QR for http://10.42.0.1. Rows are rendered as horizontal runs,
// keeping the retained scene below one batched quad per individual module.
const qr = [
  "1111111000100010001111111",
  "1000001001000100001000001",
  "1011101010010001001011101",
  "1011101001001100001011101",
  "1011101001001100101011101",
  "1000001000111011101000001",
  "1111111010101010101111111",
  "0000000010010001000000000",
  "1110111110001000011000100",
  "0001000101011100011100001",
  "1010101010111010110010111",
  "1001110101101110111100010",
  "0000111100110011101101011",
  "0111100110110010001101001",
  "1000011110100100111110111",
  "0101000100010001010101010",
  "1010001100001000111111000",
  "0000000011011100100011111",
  "1111111010111011101010011",
  "1000001011101111100011010",
  "1011101010110010111110001",
  "1011101001010011110010100",
  "1011101010100101010011001",
  "1000001010110001110011010",
  "1111111011001000100100011"
];
const moduleSize = 20;
const quiet = 4;
const qrX = 100;
const qrY = 240;
const qrSize = (qr.length + quiet * 2) * moduleSize;
fx.rect(qrX + qrSize / 2, qrY + qrSize / 2, qrSize, qrSize, 0xffffffff);
for (let y = 0; y < qr.length; y++) {
  let start = -1;
  for (let x = 0; x <= qr[y].length; x++) {
    const black = x < qr[y].length && qr[y][x] === "1";
    if (black && start < 0) start = x;
    if (!black && start >= 0) {
      const width = x - start;
      fx.rect(qrX + (quiet + start + width / 2) * moduleSize,
              qrY + (quiet + y + 0.5) * moduleSize,
              width * moduleSize, moduleSize, 0x050505ff);
      start = -1;
    }
  }
}

fx.text("1. CONNECT TO THE SETUP WI-FI", 880, 260, 29, 0xffdf62ff);
fx.text("Network", 880, 330, 19, 0x7f96c4ff);
fx.text(apSsid, 880, 362, 32, 0xffffffff);
fx.text("Password", 880, 430, 19, 0x7f96c4ff);
fx.text(apPassword, 880, 462, 32, 0xffffffff);

fx.text("2. SCAN THE QR CODE OR OPEN", 880, 560, 29, 0xffdf62ff);
fx.text(portalUrl, 880, 622, 32, 0x72e6ffff);
fx.text("Add Wi-Fi networks and change the PeerJS device name.",
        880, 674, 19, 0xb7c8eaff);

fx.text("PEERJS DEVICE NAME", 880, 770, 19, 0x7f96c4ff);
fx.gradientRect(1330, 846, 900, 78, 0x223866ff, 0x142448ff);
fx.text(peerId, 910, 827, 34, 0xffffffff);

const countdown = fx.text("Starting project in 40 seconds", 880, 966, 22, 0x72e6ffff);
let previousSecond = 40;

function update(time, delta) {
  void delta;
  const remaining = Math.max(0, Math.ceil(40 - time));
  if (remaining !== previousSecond) {
    previousSecond = remaining;
    fx.setText(countdown, "Starting project in " + remaining + " seconds");
  }
}
