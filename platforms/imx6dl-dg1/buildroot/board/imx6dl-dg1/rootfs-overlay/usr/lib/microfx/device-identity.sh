#!/bin/sh

# Load or create the stable, human-readable device identity shared by the
# setup AP, onboarding screen, and PeerJS bridge. Product defaults remain the
# fallback when /data is unavailable; a successful first boot persists the
# generated values atomically below /data/config.

microfx_identity_source=${MICROFX_IDENTITY_SOURCE:-}
microfx_identity_config=${MICROFX_IDENTITY_CONFIG:-${MICROFX_DATA_ROOT:-/data}/config/device-identity.conf}
microfx_peer_id_config=${MICROFX_PEER_ID_CONFIG:-${MICROFX_DATA_ROOT:-/data}/config/peer-id}

microfx_identity_valid() {
  case "$1" in ''|*[!A-Za-z0-9-]*) return 1 ;; esac
  [ "${#1}" -le "$2" ]
}

microfx_read_hardware_identity() {
  if [ -n "$microfx_identity_source" ]; then
    printf '%s\n' "$microfx_identity_source"
    return
  fi
  for path in \
    /sys/devices/soc0/serial_number \
    /sys/fsl_otp/HW_OCOTP_CFG0 \
    /proc/device-tree/serial-number \
    /sys/class/net/eth0/address \
    /sys/class/net/wlan1/address; do
    if [ -r "$path" ]; then
      value=$(tr -d '\000\r\n: -' <"$path" 2>/dev/null || true)
      [ -z "$value" ] || { printf '%s\n' "$value"; return; }
    fi
  done
  if command -v od >/dev/null 2>&1 && [ -r /dev/urandom ]; then
    od -An -N16 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n'
    return
  fi
  printf '%s-%s-%s\n' "$(date +%s 2>/dev/null || echo 0)" "$$" "${RANDOM:-0}"
}

microfx_identity_suffix() {
  seed=$(microfx_read_hardware_identity)
  if command -v sha256sum >/dev/null 2>&1; then
    suffix=$(printf '%s' "$seed" | sha256sum | cut -c1-12)
  elif command -v md5sum >/dev/null 2>&1; then
    suffix=$(printf '%s' "$seed" | md5sum | cut -c1-12)
  else
    suffix=$(printf '%s' "$seed" | tr -cd 'A-Fa-f0-9' | cut -c1-12)
  fi
  [ "${#suffix}" -eq 12 ] || suffix=$(printf '%012x' "$$")
  printf '%s-%s-%s\n' "$(printf '%s' "$suffix" | cut -c1-4)" \
                        "$(printf '%s' "$suffix" | cut -c5-8)" \
                        "$(printf '%s' "$suffix" | cut -c9-12)"
}

microfx_load_device_identity() {
  identity_loaded=0
  if [ -r "$microfx_identity_config" ]; then
    identity_ssid=
    identity_password=
    identity_peer=
    while IFS='=' read -r key value || [ -n "$key$value" ]; do
      case "$key" in
        MICROFX_SETUP_SSID) identity_ssid=$value;;
        MICROFX_SETUP_PASSWORD) identity_password=$value;;
        MICROFX_DEFAULT_PEER_ID) identity_peer=$value;;
      esac
    done <"$microfx_identity_config"
    MICROFX_SETUP_SSID=$identity_ssid
    MICROFX_SETUP_PASSWORD=$identity_password
    MICROFX_DEFAULT_PEER_ID=$identity_peer
    identity_loaded=1
  fi

  if [ "$identity_loaded" = 1 ] &&
     microfx_identity_valid "${MICROFX_SETUP_SSID:-}" 32 &&
     microfx_identity_valid "${MICROFX_SETUP_PASSWORD:-}" 63 &&
     [ "${#MICROFX_SETUP_PASSWORD}" -ge 8 ] &&
     microfx_identity_valid "${MICROFX_DEFAULT_PEER_ID:-}" 64; then
    :
  else
    suffix=$(microfx_identity_suffix)
    MICROFX_SETUP_SSID="${MICROFX_PRODUCT_NAME:-microFX}-$suffix"
    MICROFX_SETUP_PASSWORD="setup-$suffix"
    MICROFX_DEFAULT_PEER_ID="${MICROFX_PRODUCT_SLUG:-microfx}-$suffix"
  fi

  identity_dir=${microfx_identity_config%/*}
  if [ ! -r "$microfx_identity_config" ] && mkdir -p "$identity_dir" 2>/dev/null; then
    umask 077
    identity_new=$microfx_identity_config.new.$$
    {
      printf 'MICROFX_SETUP_SSID=%s\n' "$MICROFX_SETUP_SSID"
      printf 'MICROFX_SETUP_PASSWORD=%s\n' "$MICROFX_SETUP_PASSWORD"
      printf 'MICROFX_DEFAULT_PEER_ID=%s\n' "$MICROFX_DEFAULT_PEER_ID"
    } >"$identity_new" && mv -f "$identity_new" "$microfx_identity_config"
    rm -f "$identity_new"
  fi

  if [ ! -s "$microfx_peer_id_config" ]; then
    peer_dir=${microfx_peer_id_config%/*}
    if mkdir -p "$peer_dir" 2>/dev/null; then
      umask 077
      peer_new=$microfx_peer_id_config.new.$$
      printf '%s\n' "$MICROFX_DEFAULT_PEER_ID" >"$peer_new" &&
        mv -f "$peer_new" "$microfx_peer_id_config"
      rm -f "$peer_new"
    fi
  fi

  export MICROFX_SETUP_SSID MICROFX_SETUP_PASSWORD MICROFX_DEFAULT_PEER_ID
}
