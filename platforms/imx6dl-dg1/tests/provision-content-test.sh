#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/lib/microfx/provision-content.sh"

TEMP=$(mktemp -d "${TMPDIR:-/tmp}/microfx-captive-content.XXXXXX")
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

mkdir -p "$TEMP/hotspot-detect.html"
printf 'legacy\n' >"$TEMP/hotspot-detect.html/index.html"
printf 'portal\n' >"$TEMP/index.html"
microfx_prepare_captive_content "$TEMP"

for probe in generate_204 gen_204 ncsi.txt connecttest.txt redirect canonical.html hotspot-detect.html; do
  [ -L "$TEMP/$probe" ]
  [ "$(readlink "$TEMP/$probe")" = index.html ]
done
[ -L "$TEMP/library/test/success.html" ]
[ "$(readlink "$TEMP/library/test/success.html")" = ../../index.html ]
[ "$(cat "$TEMP/hotspot-detect.html")" = portal ]

echo "captive probe content tests passed"
