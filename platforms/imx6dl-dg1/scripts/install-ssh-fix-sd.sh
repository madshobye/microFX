#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
E2FS=/opt/homebrew/opt/e2fsprogs/sbin
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

info=$(diskutil info "$DISK")
echo "$info" | grep -q 'Device Location:.*External' || {
    echo "Refusing a disk that is not external" >&2
    exit 1
}
echo "$info" | grep -q '3965190144 Bytes' || {
    echo "Refusing a disk that is not the expected 4 GB DG1 card" >&2
    exit 1
}

check_filesystem() {
    device=$1
    set +e
    "$E2FS/e2fsck" -fy "$device"
    result=$?
    set -e
    [ "$result" -le 1 ] || exit "$result"
}

diskutil unmountDisk "$DISK"
for partition in 2 3; do
    device="${DISK}s${partition}"
    echo "===== Installing SSH startup fix in root slot $partition ====="
    check_filesystem "$device"
    "$E2FS/debugfs" -w \
      -f "$PLATFORM_DIR/artifacts/dev-update/install-ssh-fix.debugfs" \
      "$device"
    check_filesystem "$device"
done

sync
diskutil eject "$DISK"
echo "SSH startup fix installed on both DG1 root slots"
