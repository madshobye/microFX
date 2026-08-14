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
[ -x "$E2FS/e2fsck" ] && [ -x "$E2FS/debugfs" ] || {
    echo "Homebrew e2fsprogs is missing" >&2
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

commands=$(mktemp /tmp/microfx-data-recovery.XXXXXX)
trap 'rm -f "$commands"' EXIT
sed "s|@PLATFORM_DIR@|$PLATFORM_DIR|g" \
    "$PLATFORM_DIR/artifacts/dev-update/install-data-recovery.debugfs.in" >"$commands"

check_filesystem() {
    device=$1
    set +e
    "$E2FS/e2fsck" -fy "$device"
    result=$?
    set -e
    [ "$result" -le 1 ] || {
        echo "Filesystem check failed for $device (status $result)" >&2
        exit "$result"
    }
}

verify_path() {
    device=$1
    path=$2
    result=$("$E2FS/debugfs" -R "stat $path" "$device" 2>&1)
    echo "$result" | grep -q '^Inode:' || {
        echo "Verification failed: $path is missing from $device" >&2
        exit 1
    }
}

diskutil unmountDisk "$DISK"
for partition in 2 3; do
    device="${DISK}s${partition}"
    echo "===== Updating root slot $partition ====="
    check_filesystem "$device"
    "$E2FS/debugfs" -w -f "$commands" "$device"
    check_filesystem "$device"
    verify_path "$device" /data
    verify_path "$device" /etc/init.d/S39data
    verify_path "$device" /etc/init.d/S39dropbear-debug
    verify_path "$device" /etc/init.d/S39recovery-client
    verify_path "$device" /etc/init.d/S43peer-bridge
    verify_path "$device" /usr/sbin/canvas-supervisor
done

sync
diskutil eject "$DISK"
echo "Persistent-data recovery installed on both root slots"
