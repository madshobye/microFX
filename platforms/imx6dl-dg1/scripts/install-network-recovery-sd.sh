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

commands=$(mktemp /tmp/microfx-network-recovery.XXXXXX)
trap 'rm -f "$commands"' EXIT
sed "s|@PLATFORM_DIR@|$PLATFORM_DIR|g" \
    "$PLATFORM_DIR/artifacts/dev-update/install-network-recovery.debugfs.in" >"$commands"

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
        echo "$result" >&2
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
    verify_path "$device" /etc/init.d/S40provision
    verify_path "$device" /etc/init.d/S41wifi
    verify_path "$device" /usr/sbin/wifi-connect
    verify_path "$device" /usr/sbin/wifi-watchdog
done

sync
diskutil eject "$DISK"
echo "Network recovery installed on both root slots"
