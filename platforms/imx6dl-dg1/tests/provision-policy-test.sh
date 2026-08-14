#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/provision-policy.sh"

[ "$(microfx_provision_action 1 1 1 1 1 1 1 1 1 0)" = healthy ]
[ "$(microfx_provision_action 1 0 1 1 1 1 1 1 1 1)" = wait ]
[ "$(microfx_provision_action 1 0 1 1 1 1 1 1 1 3)" = repair ]
[ "$(microfx_provision_action 0 1 1 1 1 1 1 1 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 0 1 1 1 1 1 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 1 0 1 1 1 1 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 1 1 0 1 1 1 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 1 1 1 0 1 1 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 1 1 1 1 0 1 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 1 1 1 1 1 0 1 3)" = repair ]
[ "$(microfx_provision_action 1 1 1 1 1 1 1 1 0 3)" = repair ]

overlay="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay"
grep -q '^MICROFX_PROVISIONING=0$' "$overlay/etc/microfx.conf"
grep -q 'Setup network disabled by MICROFX_PROVISIONING=0' "$overlay/etc/init.d/S40provision"
grep -q 'portal_running' "$overlay/etc/init.d/S40provision"
grep -q '^ignore_broadcast_ssid=0$' "$overlay/etc/hostapd-microfx.conf"
grep -q '^ctrl_interface=/run/hostapd$' "$overlay/etc/hostapd-microfx.conf"
grep -q '^dhcp-option=114,http://10.42.0.1/$' "$overlay/etc/dnsmasq-microfx.conf"
grep -q 'httpd -f ' "$overlay/etc/init.d/S40provision"
grep -q 'hostapd failed after' "$overlay/etc/init.d/S40provision"
grep -q 'run_root=${MICROFX_RUN_ROOT:-/run}' "$overlay/etc/init.d/S40provision"
grep -q 'microfx_prepare_captive_content "$www_runtime"' "$overlay/etc/init.d/S40provision"
grep -q 'microfx_provision_beacon_ready' "$overlay/etc/init.d/S40provision"
grep -q 'Setup AP is not ready; recovery watchdog will keep retrying' \
  "$overlay/etc/init.d/S40provision"
grep -q 'setup HTTP server failed to start' "$overlay/etc/init.d/S40provision"
grep -q 'microfx_provision_http_ready' "$overlay/usr/sbin/provision-watchdog"
grep -q 'microfx_provision_beacon_ready' "$overlay/usr/sbin/provision-watchdog"

echo "setup AP recovery policy tests passed"
