# microFX Provisioning Service

This optional microFX service is separate from the graphics engine. It accepts a Wi-Fi
SSID/password and peer ID from a small captive web form, validates them in C,
and writes configuration transactionally under `/data/config`. It signals a
platform-neutral reload request through `/run/microfx-wifi-reload`; it never
invokes a platform init system directly.

Platform adapters own radio selection, DHCP/DNS, access-point security and init
integration. The i.MX6DL-DG1 platform packages this service with BusyBox HTTPD, hostapd and
dnsmasq; a Raspberry Pi or desktop target can omit it or provide another adapter
without changing `engine/` or `apps/`.

The management response also exposes client-network and setup-network health,
including whether hostapd is actually beaconing. Its project controls select or
restart an installed project through the same acknowledged renderer-health path
used by Studio. Captive-network probe routing remains a platform responsibility.

The browser controller is packaged separately as `portal-app.js`. Periodic
health refreshes preserve the user's pending project selection, Run/Restart are
locked while an acknowledged activation is in flight, and device/renderer
failures remain visible without leaving the controls wedged. Selection and
reload publication are transactional: a failed reload request restores the
previous active project.

The i.MX6DL adapter also publishes the self-contained Studio bundle at
`/studio/` from the same management HTTP server. The status CGI reports the
validated configured Peer ID, and the portal links to
`/studio/?peer=DEVICE_ID`, so Studio opens with the correct target without a
separate host web server. The editor assets remain owned by `web/editor`; this
service only exposes the platform-packaged copy.

`tests/run.sh` verifies that adding a second network preserves the first,
re-entering an existing SSID atomically replaces only that profile, escaped
SSID/password text remains valid, and only PeerJS-safe device IDs are accepted.
