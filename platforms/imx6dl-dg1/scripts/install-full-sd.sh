#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ARTIFACTS="$PLATFORM_DIR/artifacts"
IMAGE="$ARTIFACTS/microfx-imx6dl-dg1.rootfs"
DISK=${1:-}

if [ -z "$DISK" ]; then
    echo "Usage: sudo $0 /dev/diskN" >&2
    exit 2
fi
case "$DISK" in
    /dev/disk[0-9]*) ;;
    *) echo "Refusing unexpected disk path: $DISK" >&2; exit 2 ;;
esac
[ "$(id -u)" -eq 0 ] || { echo "Run this installer with sudo" >&2; exit 1; }
[ -f "$IMAGE" ] || { echo "Missing image: $IMAGE" >&2; exit 1; }
[ "$(stat -f %z "$IMAGE")" = "1610612736" ] || {
    echo "Refusing image with unexpected size" >&2
    exit 1
}

info=$(diskutil info "$DISK")
echo "$info" | grep -q 'Device Location:.*External' || {
    echo "Refusing a disk that is not external" >&2
    exit 1
}
echo "$info" | grep -q '3965190144 Bytes' || {
    echo "Refusing a disk that is not the expected 4 GB card" >&2
    exit 1
}

disk_name=${DISK#/dev/}
layout=$(diskutil list "$DISK")
echo "$layout" | grep -q "1.6 GB.*${disk_name}s2" || { echo "Missing root slot 2" >&2; exit 1; }
echo "$layout" | grep -q "1.6 GB.*${disk_name}s3" || { echo "Missing root slot 3" >&2; exit 1; }
echo "$layout" | grep -q "584.6 MB.*${disk_name}s4" || { echo "Missing data partition" >&2; exit 1; }

(
    cd "$ARTIFACTS"
    shasum -a 256 -c SHA256SUMS
)

diskutil unmountDisk "$DISK"
for partition in 2 3; do
    target="/dev/r${disk_name}s${partition}"
    echo "===== Writing complete microFX root slot $partition ====="
    dd if="$IMAGE" of="$target" bs=8m
done

sync
diskutil eject "$DISK"
echo "Complete microFX image installed on both root slots"
