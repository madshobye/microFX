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
    echo "Refusing a disk that is not the expected 4 GB DG1 card" >&2
    exit 1
}

disk_name=${DISK#/dev/}
layout=$(diskutil list "$DISK")
echo "$layout" | grep -q "1.6 GB.*${disk_name}s2" || { echo "Missing DG1 root slot 2" >&2; exit 1; }
echo "$layout" | grep -q "1.6 GB.*${disk_name}s3" || { echo "Missing DG1 root slot 3" >&2; exit 1; }
echo "$layout" | grep -q "584.6 MB.*${disk_name}s4" || { echo "Missing DG1 data partition" >&2; exit 1; }

commands=$(mktemp /tmp/microfx-development.XXXXXX)
slot_commands=
trap 'rm -f "$commands" ${slot_commands:+"$slot_commands"}' EXIT
cp "$PLATFORM_DIR/artifacts/dev-update/install-development.debugfs" "$commands"

check_filesystem() {
    device=$1
    set +e
    "$E2FS/e2fsck" -fy "$device"
    result=$?
    set -e
    # e2fsck status 1 means errors were found and successfully corrected.
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
    echo "===== Updating DG1 root slot $partition ====="
    check_filesystem "$device"

    slot_commands="${commands}.${partition}"
    cp "$commands" "$slot_commands"
    # debugfs returns success even when `stat` reports a missing path, so it
    # cannot be used as a shell existence test. Keep the mkdir commands in the
    # update stream; debugfs continues harmlessly when a directory exists.
    "$E2FS/debugfs" -w -f "$slot_commands" "$device"
    rm -f "$slot_commands"

    check_filesystem "$device"
    verify_path "$device" /usr/sbin/dropbear
    verify_path "$device" /root/.ssh/authorized_keys
done

sync
diskutil eject "$DISK"
echo "Development setup installed on both DG1 root slots"
