#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
OUTPUT=${2:-}
SSH_WRAPPER=${MICROFX_SSH_WRAPPER:-$SCRIPT_DIR/canvas-ssh.sh}

[ -x "$SSH_WRAPPER" ] || {
  echo "Missing SSH wrapper: $SSH_WRAPPER" >&2
  exit 1
}

collect() {
  "$SSH_WRAPPER" "$HOST" '
set -u
section() { printf "\n===== %s =====\n" "$1"; }

section identity
printf "utc="; date -u +%Y-%m-%dT%H:%M:%SZ
now_epoch=$(date +%s 2>/dev/null || echo 0)
if [ "$now_epoch" -ge 1704067200 ] 2>/dev/null; then echo "clock_sane=yes"; else echo "clock_sane=no"; fi
printf "uptime_seconds="; cut -d" " -f1 /proc/uptime
printf "hostname="; hostname
printf "kernel="; uname -r
printf "cmdline="; cat /proc/cmdline
root_device=$(awk "\$2 == \"/\" { print \$1; exit }" /proc/mounts)
echo "root_device=${root_device:-unknown}"
cmdline_root=$(sed -n "s/.* root=\([^ ]*\).*/\1/p" /proc/cmdline)
echo "cmdline_root=${cmdline_root:-unknown}"
root_source=$root_device
[ "$root_source" != /dev/root ] || root_source=$cmdline_root
echo "root_source=${root_source:-unknown}"
case "$root_source" in
  /dev/mmcblk0p2) echo "root_slot=2" ;;
  /dev/mmcblk0p3) echo "root_slot=3" ;;
  *) echo "root_slot=unknown" ;;
esac
if [ -r /etc/microfx-release ]; then
  sed -n "s/^MICROFX_IMAGE_SCHEMA=/image_schema=/p; s/^MICROFX_PLATFORM=/image_platform=/p; s/^MICROFX_BOOT_MODEL=/image_boot_model=/p" /etc/microfx-release
else
  echo "image_schema=missing"
  echo "image_platform=unknown"
  echo "image_boot_model=unknown"
fi

section persistence
mount | grep -E " on /(data|) " || true
data_device=$(awk "\$2 == \"/data\" { print \$1; exit }" /proc/mounts)
echo "data_device=${data_device:-missing}"
printf "active_project="
basename "$(readlink /data/apps/current 2>/dev/null)" 2>/dev/null || echo unavailable
for required in /data/apps/projects /data/config /data/state; do
  if [ -d "$required" ]; then echo "$required=present"; else echo "$required=missing"; fi
done

section renderer
if pidof canvas-demo >/dev/null 2>&1; then echo "renderer=running"; else echo "renderer=down"; fi
tail -n 80 /tmp/canvas.log 2>/dev/null |
  grep -E "MICROFX_PROFILE|DRM_TIMING|ERROR|failed|running project" | tail -n 20 || true

section wifi_client
product_config=/etc/microfx-product.conf
[ -r "$product_config" ] && . "$product_config"
client_interface=${MICROFX_WIFI_INTERFACE:-wlan1}
provision_interface=${MICROFX_PROVISION_INTERFACE:-wlan0}
echo "client_interface=$client_interface"
wpa_cli -i "$client_interface" status 2>/dev/null |
  grep -E "^(wpa_state|ssid|bssid|freq|ip_address)=" || true
iw dev "$client_interface" link 2>/dev/null |
  grep -E "^Connected|SSID:|freq:|signal:|tx bitrate:" || true
iw dev "$client_interface" info 2>/dev/null | grep -E "type |txpower" || true
grep -E "${client_interface}:" /proc/net/dev || true

section setup_ap
echo "provision_interface=$provision_interface"
iw dev "$provision_interface" info 2>/dev/null | grep -E "ssid |type |channel |txpower" || true
for service in hostapd dnsmasq httpd; do
  pidfile=/run/${service}-microfx.pid
  if [ -r "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
    echo "$service=running"
  else
    echo "$service=down"
  fi
done
hostapd_status=$(hostapd_cli -p /run/hostapd -i "$provision_interface" status 2>/dev/null ||
  hostapd_cli -p /var/run/hostapd -i "$provision_interface" status 2>/dev/null ||
  hostapd_cli -i "$provision_interface" status 2>/dev/null || true)
printf "%s\n" "$hostapd_status" | grep -E "^(state|ssid\[0\]|channel|num_sta)=" || true
if wget -q -T 3 -O /dev/null http://127.0.0.1/; then
  echo "portal_http=ok"
else
  echo "portal_http=failed"
fi

section services
if pidof microfx-peer-bridge >/dev/null 2>&1; then echo "peer_bridge=running"; else echo "peer_bridge=down"; fi
if pidof ntpd >/dev/null 2>&1 || pidof chronyd >/dev/null 2>&1; then echo "time_service=running"; else echo "time_service=exited"; fi
if [ "$now_epoch" -ge 1704067200 ] 2>/dev/null; then
  echo "time_sync=valid"
elif pidof ntpd >/dev/null 2>&1 || pidof chronyd >/dev/null 2>&1; then
  echo "time_sync=syncing"
else
  echo "time_sync=invalid"
fi

section firmware_diagnostics
board_data_path=/lib/firmware/ath6k/AR6003/hw2.1.1/bdata.bin
if [ -r "$board_data_path" ]; then
  echo "board_data_file=present"
  echo "board_data_target=$(readlink "$board_data_path" 2>/dev/null || echo not-symlink)"
  echo "board_data_bytes=$(wc -c <"$board_data_path" | tr -d "[:space:]")"
  if command -v sha256sum >/dev/null 2>&1; then
    set -- $(sha256sum "$board_data_path")
    echo "board_data_sha256=$1"
  else
    echo "board_data_sha256=unavailable"
  fi
else
  echo "board_data_file=missing"
  echo "board_data_target=missing"
  echo "board_data_bytes=0"
  echo "board_data_sha256=unavailable"
fi
board_data_fallback_warnings=$(dmesg 2>/dev/null |
  grep -E -c "Failed to get board file|No proper board file|using a default board file" || true)
echo "board_data_fallback_warnings=$board_data_fallback_warnings"
dmesg 2>/dev/null |
  grep -E "ath6kl|bdata\.bin|regulatory\.db|firmware request failed" | tail -n 30 || true
'
}

if [ -n "$OUTPUT" ]; then
  mkdir -p "$(dirname -- "$OUTPUT")"
  collect | tee "$OUTPUT"
  echo "Saved hardware smoke report to $OUTPUT" >&2
else
  collect
fi
