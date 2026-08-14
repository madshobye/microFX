#!/bin/sh
set -eu

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
[ "$(id -u)" -eq 0 ] || { echo "Run this diagnostic with sudo" >&2; exit 1; }
[ -x "$E2FS/debugfs" ] || { echo "Homebrew e2fsprogs is missing" >&2; exit 1; }

info=$(diskutil info "$DISK")
echo "$info" | grep -q 'Device Location:.*External' || {
    echo "Refusing a disk that is not external" >&2
    exit 1
}
echo "$info" | grep -q '3965190144 Bytes' || {
    echo "Refusing a disk that is not the expected 4 GB DG1 card" >&2
    exit 1
}

diskutil unmountDisk "$DISK" >/dev/null

show_stat() {
    device=$1
    path=$2
    echo "--- stat $path"
    "$E2FS/debugfs" -R "stat $path" "$device" 2>&1 || true
}

show_file() {
    device=$1
    path=$2
    echo "--- cat $path"
    "$E2FS/debugfs" -R "cat $path" "$device" 2>&1 || true
}

for partition in 2 3; do
    device="${DISK}s${partition}"
    echo "===== ROOT SLOT $partition ====="
    show_file "$device" /etc/canvas.conf
    show_stat "$device" /data
    show_stat "$device" /usr/sbin/dropbear
    show_stat "$device" /usr/lib/libcrypt.so.2
    show_stat "$device" /root/.ssh/authorized_keys
    show_file "$device" /etc/init.d/S39dropbear-debug
    show_file "$device" /etc/init.d/S39recovery-client
    show_file "$device" /etc/init.d/S41wifi
    show_file "$device" /usr/sbin/microfx-recovery-guardian
    show_file "$device" /usr/sbin/microfx-recovery-client
done

device="${DISK}s4"
echo "===== DATA PARTITION 4 ====="
for path in / /ssh /state /state/root-update-backups /recovery /apps /apps/current /apps/releases /apps/releases/factory; do
    echo "--- ls $path"
    "$E2FS/debugfs" -R "ls -l $path" "$device" 2>&1 || true
done
show_stat "$device" /ssh/dropbear_ed25519_host_key
show_file "$device" /state/dropbear.log
show_file "$device" /state/canvas.log
show_file "$device" /state/wifi.log
show_file "$device" /state/wpa_supplicant.log
show_file "$device" /state/boot-count
show_file "$device" /state/short-boot-count
show_file "$device" /state/boot-pending
show_file "$device" /state/boot-stable
show_stat "$device" /config/wpa_supplicant.conf
show_stat "$device" /apps/current
show_stat "$device" /apps/current-runtime
show_stat "$device" /apps/previous-runtime
show_file "$device" /apps/projects/demo-scene/main.js

echo "Read-only diagnostic complete; $DISK remains unmounted"
