#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/radio-policy.sh"

[ "$(microfx_ap_mac 00:03:7f:be:f0:a0)" = "02:03:7f:be:f0:a1" ]
[ "$(microfx_ap_mac 01:ff:00:00:00:ff)" = "02:ff:00:00:00:00" ]
if microfx_ap_mac invalid >/dev/null 2>&1; then
  echo "invalid radio address was accepted" >&2
  exit 1
fi

echo "radio identity policy tests passed"
