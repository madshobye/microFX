#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
library="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/network-status.sh"
connect="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/wifi-connect"
watchdog="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/wifi-watchdog"
grep -q 'microfx_write_network_status' "$library"
grep -q 'microfx_write_network_status connected' "$connect"
grep -q 'control_socket=.*wpa_supplicant.*interface' "$connect"
grep -q 'rm -f.*control_socket' "$connect"
grep -q 'stop_pidfile.*wpa_pid' "$connect"
grep -q 'awaiting-address' "$watchdog"
grep -q 'reconnecting' "$watchdog"
grep -q 'MICROFX_RUN_ROOT:-/run' "$library"
grep -q 'temporary=.*microfx-network-status.new' "$library"
grep -q 'mv -f.*microfx-network-status' "$library"

echo "RAM-only network status tests passed"
