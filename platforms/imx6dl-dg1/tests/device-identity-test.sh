#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LIBRARY=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/device-identity.sh
TEMP=$(mktemp -d "${TMPDIR:-/tmp}/microfx-identity-test.XXXXXX")
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

load_identity() {
  MICROFX_PRODUCT_NAME=microFX
  MICROFX_PRODUCT_SLUG=microfx
  MICROFX_SETUP_SSID=
  MICROFX_SETUP_PASSWORD=
  MICROFX_DEFAULT_PEER_ID=
  MICROFX_IDENTITY_SOURCE=$1
  MICROFX_IDENTITY_CONFIG=$TEMP/config/device-identity.conf
  MICROFX_PEER_ID_CONFIG=$TEMP/config/peer-id
  . "$LIBRARY"
  microfx_load_device_identity
}

load_identity board-serial-0001
first_ssid=$MICROFX_SETUP_SSID
first_password=$MICROFX_SETUP_PASSWORD
first_peer=$MICROFX_DEFAULT_PEER_ID

case "$first_ssid" in microFX-????-????-????) ;; *) exit 1 ;; esac
case "$first_password" in setup-????-????-????) ;; *) exit 1 ;; esac
case "$first_peer" in microfx-????-????-????) ;; *) exit 1 ;; esac
[ "$(cat "$TEMP/config/peer-id")" = "$first_peer" ]

# Persisted identity wins when the serial source later changes, and a portal
# edit of peer-id is never overwritten on a later boot.
printf '%s\n' custom-peer >"$TEMP/config/peer-id"
load_identity different-hardware-source
[ "$MICROFX_SETUP_SSID" = "$first_ssid" ]
[ "$MICROFX_SETUP_PASSWORD" = "$first_password" ]
[ "$MICROFX_DEFAULT_PEER_ID" = "$first_peer" ]
[ "$(cat "$TEMP/config/peer-id")" = custom-peer ]

echo "device identity tests passed"
