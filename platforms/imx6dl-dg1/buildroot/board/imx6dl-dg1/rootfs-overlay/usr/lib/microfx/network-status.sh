#!/bin/sh

microfx_network_field() {
  printf '%s' "$1" | tr '\t\r\n' '   '
}

# Write a small, atomic, RAM-only snapshot for the portal and diagnostics.
microfx_write_network_status() {
  state=${1:-unknown}
  detail=${2:-}
  interface=${3:-${MICROFX_WIFI_INTERFACE:-wlan1}}
  status=$(wpa_cli -i "$interface" status 2>/dev/null || true)
  ssid=$(printf '%s\n' "$status" | sed -n 's/^ssid=//p' | head -n 1)
  bssid=$(printf '%s\n' "$status" | sed -n 's/^bssid=//p' | head -n 1)
  frequency=$(printf '%s\n' "$status" | sed -n 's/^freq=//p' | head -n 1)
  address=$(ip -4 address show dev "$interface" 2>/dev/null | sed -n 's/.*inet \([^ ]*\).*/\1/p' | head -n 1)
  link=$(iw dev "$interface" link 2>/dev/null || true)
  signal=$(printf '%s\n' "$link" | sed -n 's/^[[:space:]]*signal: \(-*[0-9]*\).*/\1/p' | head -n 1)
  bitrate=$(printf '%s\n' "$link" | sed -n 's/^[[:space:]]*tx bitrate: \([^ ]*\).*/\1/p' | head -n 1)
  txpower=$(iw dev "$interface" info 2>/dev/null | sed -n 's/^[[:space:]]*txpower \([^ ]*\).*/\1/p' | head -n 1)
  run_root=${MICROFX_RUN_ROOT:-/run}
  temporary=$run_root/microfx-network-status.new
  {
    printf 'state\t'; microfx_network_field "$state"; printf '\n'
    printf 'detail\t'; microfx_network_field "$detail"; printf '\n'
    printf 'ssid\t'; microfx_network_field "$ssid"; printf '\n'
    printf 'bssid\t'; microfx_network_field "$bssid"; printf '\n'
    printf 'frequency\t'; microfx_network_field "$frequency"; printf '\n'
    printf 'address\t'; microfx_network_field "$address"; printf '\n'
    printf 'signal\t'; microfx_network_field "$signal"; printf '\n'
    printf 'bitrate\t'; microfx_network_field "$bitrate"; printf '\n'
    printf 'txpower\t'; microfx_network_field "$txpower"; printf '\n'
    printf 'updated\t%s\n' "$(date +%s)"
  } >"$temporary"
  mv -f "$temporary" "$run_root/microfx-network-status"
}
