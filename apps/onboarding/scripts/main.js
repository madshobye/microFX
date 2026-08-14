// Firmware onboarding application. It is an ordinary retained JavaScript
// scene; the platform supplies stable generated identity and the Wi-Fi QR.
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
const provisioningEnabled = fx.env("MICROFX_PROVISIONING", "0") === "1";
const captivePortal = fx.env("MICROFX_CAPTIVE_PORTAL", "0") === "1";
const portalUrl = "http://10.42.0.1";

fx.rect(960, 540, 1920, 1080, 0x000000ff);
fx.text(fx.product.name, 92, 52, 52, 0xffffffff);
fx.text(provisioningEnabled ? "SETUP" : "STORED-NETWORK MODE",
        94, 122, 22, 0xaaaaaaff);

if (provisioningEnabled) {
  const wifiPayload = "WIFI:T:WPA;S:" + apSsid + ";P:" + apPassword + ";;";
  fx.qr(wifiPayload, 100, 220, 620);
  fx.text("SCAN TO JOIN SETUP WI-FI",
          820, 244, 28, 0xffffffff);
  fx.text("Network", 820, 324, 18, 0x999999ff);
  fx.text(apSsid, 820, 358, 34, 0xffffffff);
  fx.text("Password", 820, 436, 18, 0x999999ff);
  fx.text(apPassword, 820, 470, 34, 0xffffffff);
  fx.text(captivePortal ? "The setup page opens after you connect." : "Then open the setup page:",
          820, 560, 22, 0xccccccff);
  fx.text(portalUrl, 820, 606, 30, 0xffffffff);
  fx.text("Add a Wi-Fi network or change the PeerJS name.",
          820, 666, 18, 0x999999ff);
  fx.text("PEERJS NAME", 820, 752, 18, 0x999999ff);
  fx.text(peerId, 820, 790, 32, 0xffffffff);
} else {
  fx.qr(peerId, 100, 220, 620);
  fx.text("SCAN PEERJS NAME", 820, 244, 28, 0xffffffff);
  fx.text("The setup access point is disabled.", 820, 324, 24, 0xccccccff);
  fx.text("Client Wi-Fi, key-only SSH, and PeerJS remain available.",
          820, 372, 19, 0x999999ff);
  fx.text("PEERJS NAME", 820, 500, 18, 0x999999ff);
  fx.text(peerId, 820, 540, 38, 0xffffffff);
}

const countdown = fx.text("Starting project in 40 seconds", 94, 994, 20, 0xaaaaaaff);
let previousSecond = 40;

function update(time, delta) {
  void delta;
  const remaining = Math.max(0, Math.ceil(40 - time));
  if (remaining !== previousSecond) {
    previousSecond = remaining;
    countdown.text("Starting project in " + remaining + " seconds");
  }
}
