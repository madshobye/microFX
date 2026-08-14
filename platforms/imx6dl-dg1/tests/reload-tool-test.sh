#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-reload-project"
profile="$ROOT/scripts/canvas-profile.sh"
grep -q "printf '%s\\\\t%s\\\\n'" "$tool"
grep -q 'mv -f.*microfx-project-reload' "$tool"
grep -q '/usr/sbin/microfx-reload-project' "$profile"
! grep -q 'touch .*microfx-project-reload' "$profile"
grep -q 'txpower' "$profile"
grep -q 'interface counters' "$profile"
grep -q 'MICROFX_WIFI_INTERFACE' "$profile"
grep -q 'MICROFX_PROVISION_INTERFACE' "$profile"
! grep -q 'wpa_cli -i wlan1' "$profile"
! grep -q 'iw dev wlan0' "$profile"
grep -q 'report|matrix' "$profile"
grep -q -- '--matrix' "$profile"

echo "reload and radio profiling tool tests passed"
