#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${CANVAS_HOST:-192.168.3.109}
ACTION=${1:-status}

case "$ACTION" in
  on|off|status|report|matrix) shift || true ;;
  *) HOST=$ACTION; ACTION=${2:-status}; shift 2 || true ;;
esac

case "$ACTION" in
  on)
    "$PLATFORM_DIR/scripts/canvas-ssh.sh" "$HOST" \
      'touch /run/microfx-profile; project=$(basename "$(readlink /data/apps/current)"); /usr/sbin/microfx-reload-project "$project" profile-on-$(date +%s)'
    echo "Profiling enabled in RAM; the renderer is restarting."
    ;;
  off)
    "$PLATFORM_DIR/scripts/canvas-ssh.sh" "$HOST" \
      'rm -f /run/microfx-profile; project=$(basename "$(readlink /data/apps/current)"); /usr/sbin/microfx-reload-project "$project" profile-off-$(date +%s)'
    echo "Profiling disabled; the renderer is restarting."
    ;;
  status)
    "$PLATFORM_DIR/scripts/canvas-ssh.sh" "$HOST" '
      product_config=${MICROFX_PRODUCT_CONFIG:-/etc/microfx-product.conf}
      [ -r "$product_config" ] && . "$product_config"
      client_interface=${MICROFX_WIFI_INTERFACE:-wlan1}
      provision_interface=${MICROFX_PROVISION_INTERFACE:-wlan0}
      if [ -e /run/microfx-profile ]; then echo enabled; else echo disabled; fi
      echo "=== Wi-Fi client ==="
      echo "interface=$client_interface"
      wpa_cli -i "$client_interface" status 2>/dev/null | grep -E "^(wpa_state|ssid|bssid|freq|ip_address)=" || true
      iw dev "$client_interface" link 2>/dev/null | grep -E "^Connected|SSID:|freq:|signal:|tx bitrate:" || true
      iw dev "$client_interface" info 2>/dev/null | grep -E "type |txpower" || true
      echo "=== setup AP ==="
      echo "interface=$provision_interface"
      iw dev "$provision_interface" info 2>/dev/null | grep -E "ssid |type |channel |txpower" || true
      for service in hostapd dnsmasq httpd; do
        pidfile=/run/${service}-microfx.pid
        if [ -r "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
          echo "$service=running"
        else
          echo "$service=down"
        fi
      done
      echo "=== regulatory ==="
      iw reg get 2>/dev/null | sed -n "1,4p" || true
      echo "=== interface counters ==="
      grep -E "($client_interface|$provision_interface):" /proc/net/dev || true
      echo "=== renderer ==="
      tail -n 400 /tmp/canvas.log 2>/dev/null |
        grep -E "MICROFX_PROFILE|DRM_TIMING" | tail -n 20 || true'
    ;;
  report|matrix)
    if [ "$ACTION" = matrix ]; then matrix=--matrix; else matrix=; fi
    "$PLATFORM_DIR/scripts/canvas-ssh.sh" "$HOST" \
      'tail -n 2000 /tmp/canvas.log 2>/dev/null | grep -E "MICROFX_PROFILE|DRM_TIMING" || true' |
      python3 "$PLATFORM_DIR/../../engine/tools/profile-report.py" $matrix
    ;;
  *)
    echo "Usage: $0 [HOST] on|off|status|report|matrix" >&2
    exit 2
    ;;
esac
