#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/wifi-policy.sh"

expect() {
  expected=$1; shift
  actual=$(microfx_wifi_action "$@")
  [ "$actual" = "$expected" ] || {
    echo "expected $expected, got $actual for: $*" >&2
    exit 1
  }
}

expect wait COMPLETED 1 0 0
expect wait SCANNING 0 1 1
expect reassociate DISCONNECTED 0 3 3
expect wait DISCONNECTED 0 4 4
expect rebuild DISCONNECTED 0 9 9
expect renew-dhcp COMPLETED 0 0 3

echo "Wi-Fi recovery policy tests passed"
