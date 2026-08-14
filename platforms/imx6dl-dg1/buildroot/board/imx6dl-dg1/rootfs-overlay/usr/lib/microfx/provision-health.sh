#!/bin/sh

# Runtime health probes for the local setup network. Keep these read-only:
# recovery is owned by S40provision, so a failed probe can never disturb the
# independent client connection selected by MICROFX_WIFI_INTERFACE.
microfx_provision_ap_mode() {
  interface=${1:-${MICROFX_PROVISION_INTERFACE:-wlan0}}
  iw dev "$interface" info 2>/dev/null | grep -q '^[[:space:]]*type AP$'
}

microfx_provision_link_up() {
  interface=${1:-${MICROFX_PROVISION_INTERFACE:-wlan0}}
  ip -o link show dev "$interface" 2>/dev/null |
    grep -q '<[^>]*UP[^>]*>'
}

microfx_provision_address_ready() {
  interface=${1:-${MICROFX_PROVISION_INTERFACE:-wlan0}}
  address=${2:-10.42.0.1/24}
  ip -o -4 address show dev "$interface" 2>/dev/null |
    grep -Fq " $address "
}

microfx_provision_beacon_ready() {
  interface=${1:-${MICROFX_PROVISION_INTERFACE:-wlan0}}
  control=${2:-/run/hostapd}
  hostapd_cli -p "$control" -i "$interface" status 2>/dev/null |
    grep -q '^state=ENABLED$'
}

microfx_provision_http_ready() {
  url=${1:-http://10.42.0.1/}
  wget -q -T 2 -O /dev/null "$url" >/dev/null 2>&1
}
