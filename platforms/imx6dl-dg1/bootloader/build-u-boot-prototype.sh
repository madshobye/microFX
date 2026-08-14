#!/bin/sh
set -eu

usage() {
  cat >&2 <<'EOF'
usage: build-u-boot-prototype.sh UPSTREAM_U_BOOT_2025_01 CROSS_COMPILE OUTPUT_DIR

Build the isolated microFX U-Boot prototype without modifying the supplied
upstream tree. CROSS_COMPILE is the full compiler prefix, for example:
  /opt/toolchains/bin/arm-buildroot-linux-gnueabihf-

This script neither changes the normal firmware build nor writes an SD card.
EOF
  exit 2
}

[ "$#" -eq 3 ] || usage

UPSTREAM=$1
CROSS_COMPILE=$2
OUTPUT=$3
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Keep the proof artifact stable across build dates unless the caller supplies
# a release epoch explicitly.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1735689600}
export SOURCE_DATE_EPOCH

[ -f "$UPSTREAM/Makefile" ] || {
  echo "upstream U-Boot tree not found: $UPSTREAM" >&2
  exit 1
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-u-boot.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
SOURCE=$WORK/source
BUILD=$WORK/build
mkdir -p "$SOURCE" "$BUILD" "$OUTPUT"

# A streamed copy keeps the caller's pristine upstream checkout untouched.
(cd "$UPSTREAM" && tar -cf - .) | (cd "$SOURCE" && tar -xf -)
"$HERE/apply-u-boot-overlay.py" "$SOURCE"

make -C "$SOURCE" O="$BUILD" CROSS_COMPILE="$CROSS_COMPILE" \
  microfx_imx6dl_dg1_defconfig
make -C "$SOURCE" O="$BUILD" CROSS_COMPILE="$CROSS_COMPILE" \
  -j"${MICROFX_BUILD_JOBS:-2}" u-boot.imx

cp "$BUILD/u-boot.imx" "$OUTPUT/u-boot.imx"
cp "$BUILD/.config" "$OUTPUT/u-boot.config"
sha256sum "$OUTPUT/u-boot.imx" >"$OUTPUT/u-boot.imx.sha256"

echo "Isolated U-Boot prototype written to $OUTPUT/u-boot.imx"
