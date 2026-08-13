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

`tests/run.sh` verifies that adding a second network preserves the first,
configuration quoting is valid, and only PeerJS-safe device IDs are accepted.
