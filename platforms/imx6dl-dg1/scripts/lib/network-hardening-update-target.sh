#!/bin/sh
set -eu

archive=${1:-}
release=${2:-}
expected_digest=${3:-}

case "$release" in
  ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID" >&2; exit 1 ;;
esac
[ -f "$archive" ] || { echo "Missing update archive: $archive" >&2; exit 1; }
[ -n "$expected_digest" ] || { echo "Missing archive checksum" >&2; exit 1; }
[ "$(sha256sum "$archive" | sed 's/[[:space:]].*//')" = "$expected_digest" ] || {
  echo "Update archive checksum mismatch" >&2
  exit 1
}

root_device=$(sed -n 's/.*root=\([^ ]*\).*/\1/p' /proc/cmdline)
case "$root_device" in
  /dev/mmcblk0p2|/dev/mmcblk0p3) ;;
  *) echo "Refusing network hardening on $root_device" >&2; exit 1 ;;
esac
grep -q ' /data ' /proc/mounts || { echo "/data is not mounted" >&2; exit 1; }
ip route | grep -q '^default .* dev wlan1' || { echo "Client Wi-Fi route is not healthy" >&2; exit 1; }
ps w | grep '[d]ropbear' >/dev/null || { echo "Debug SSH is not running" >&2; exit 1; }

stage=/tmp/microfx-network-hardening-stage
backup_dir=/data/state/root-update-backups
backup=$backup_dir/$release.tar
rm -rf "$stage"
mkdir -p "$stage" "$backup_dir"
tar -xf "$archive" -C "$stage"

required='etc/init.d/S39dropbear-debug
etc/init.d/S39recovery-client
etc/init.d/S40canvas
etc/init.d/S41wifi
usr/sbin/microfx-recovery-guardian
usr/sbin/microfx-recovery-client
usr/sbin/wifi-connect
usr/sbin/wifi-watchdog'

for path in $required; do
  [ -f "$stage/$path" ] || { echo "Archive is missing $path" >&2; exit 1; }
  sh -n "$stage/$path" || { echo "Shell validation failed: $path" >&2; exit 1; }
done

existing=
for path in $required etc/init.d/S39network-prime etc/init.d/S42dropbear-debug; do
  [ ! -e "/$path" ] || existing="$existing $path"
done
[ -z "$existing" ] || tar -cf "$backup" -C / $existing

for path in $required; do
  mkdir -p "/$(dirname "$path")"
  cp "$stage/$path" "/$path.new"
  chmod 0755 "/$path.new"
  mv -f "/$path.new" "/$path"
done

# The replacement starts earlier and is pidfile-idempotent. The old late
# service must not remain or it would launch a second Dropbear at next boot.
rm -f /etc/init.d/S39network-prime /etc/init.d/S42dropbear-debug

test -x /etc/init.d/S39recovery-client
grep -q 'microfx-graphics-safe-mode' /etc/init.d/S40canvas
grep -q 'bounded recovery failed' /usr/sbin/microfx-recovery-guardian
ip route | grep -q '^default .* dev wlan1' || { echo "Wi-Fi route disappeared during update" >&2; exit 1; }
ps w | grep '[d]ropbear' >/dev/null || { echo "SSH disappeared during update" >&2; exit 1; }

echo "active_root\t$root_device"
echo "backup\t$backup"
echo "live_network_restarted\tno"

rm -rf "$stage" "$archive" "$0"
