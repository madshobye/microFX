#!/bin/sh
set -eu

TARGET_DIR=${1:?usage: verify-target-firmware.sh TARGET_DIR}
AR6003_DIR="$TARGET_DIR/lib/firmware/ath6k/AR6003/hw2.1.1"
BOARD_DATA="$AR6003_DIR/bdata.bin"

[ -L "$BOARD_DATA" ] || {
  echo "AR6003 bdata.bin is not an explicit symlink" >&2
  exit 1
}
[ "$(readlink "$BOARD_DATA")" = bdata.SD31.bin ] || {
  echo "AR6003 bdata.bin does not select bdata.SD31.bin" >&2
  exit 1
}
[ "$(wc -c <"$BOARD_DATA" | tr -d '[:space:]')" = 1792 ] || {
  echo "AR6003 bdata.bin has an invalid size" >&2
  exit 1
}

for required in \
  "$AR6003_DIR/fw-2.bin" \
  "$AR6003_DIR/fw-3.bin" \
  "$TARGET_DIR/lib/firmware/regulatory.db" \
  "$TARGET_DIR/lib/firmware/regulatory.db.p7s"; do
  [ -s "$required" ] || {
    echo "missing required wireless firmware artifact: $required" >&2
    exit 1
  }
done

for search_root in "$TARGET_DIR/etc" "$TARGET_DIR/usr"; do
  [ -d "$search_root" ] || continue
  if grep -R -E 'modprobe[[:space:]]+ath6kl(_core)?[^#]*[[:space:]]mac=|options[[:space:]]+ath6kl(_core)?[^#]*[[:space:]]mac=' \
       "$search_root" >/dev/null 2>&1; then
    echo "obsolete ath6kl mac module parameter found in target filesystem" >&2
    exit 1
  fi
done
