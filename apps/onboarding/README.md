# microFX Onboarding App

Portable microFX JavaScript scene shown once for 40 seconds after each firmware boot.
It owns the full frame, displays the setup AP credentials and configured PeerJS
device ID, and renders a validated QR code for `http://10.42.0.1` using retained
row rectangles in the normal quad batch.

The platform supervisor supplies `MICROFX_PEER_ID`, `MICROFX_AP_SSID` and
`MICROFX_AP_PASSWORD`. Other platforms can launch the same script with equivalent
environment values or use its fallbacks. Network and captive-portal behavior do
not live in this app or the rendering engine.
