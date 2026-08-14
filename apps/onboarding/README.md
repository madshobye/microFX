# microFX Onboarding App

Portable microFX JavaScript scene shown once for 40 seconds after each firmware boot.
It owns the full frame and displays the configured PeerJS device ID. When
provisioning is enabled, it also displays setup AP credentials and renders a
validated QR code for `http://10.42.0.1` using retained row rectangles in the
normal quad batch. The default development policy instead shows that the setup
access point is disabled and client Wi-Fi is active.

The platform supervisor supplies `MICROFX_PEER_ID`, `MICROFX_PROVISIONING`,
`MICROFX_AP_SSID` and `MICROFX_AP_PASSWORD`. Other platforms can launch the same
script with equivalent environment values or use its fallbacks. Network and
captive-portal behavior do not live in this app or the rendering engine.
