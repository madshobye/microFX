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

actual_digest=$(sha256sum "$archive" | sed 's/[[:space:]].*//')
[ "$actual_digest" = "$expected_digest" ] || {
  echo "Update archive checksum mismatch" >&2
  exit 1
}

cmdline=$(cat /proc/cmdline)
root_device=$(printf '%s\n' "$cmdline" | sed -n 's/.*root=\([^ ]*\).*/\1/p')
case "$root_device" in
  /dev/mmcblk0p2|/dev/mmcblk0p3) ;;
  *) echo "Refusing active-root update on $root_device" >&2; exit 1 ;;
esac

grep -q ' /data ' /proc/mounts || { echo "/data is not mounted" >&2; exit 1; }
ip route | grep -q '^default .* dev wlan1' || {
  echo "Client Wi-Fi default route is not healthy" >&2
  exit 1
}
ps w | grep '[d]ropbear' >/dev/null || { echo "Debug SSH is not running" >&2; exit 1; }

stage=/tmp/microfx-active-root-update
backup_dir=/data/state/root-update-backups
backup=$backup_dir/$release.tar
rm -rf "$stage"
mkdir -p "$stage" "$backup_dir"
tar -xf "$archive" -C "$stage"

required='boot/uEnv.txt
boot/uEnv-debug.txt
etc/fstab
etc/microfx.conf
etc/microfx-release
etc/init.d/S39seedrng
etc/init.d/S39data
etc/init.d/S40canvas
etc/init.d/S40provision
etc/init.d/S45status
usr/sbin/canvas-supervisor
usr/sbin/microfx-benchmark-override
usr/sbin/microfx-benchmark-capture
usr/sbin/microfx-status
usr/share/microfx/onboarding.js'

for path in $required; do
  [ -f "$stage/$path" ] || { echo "Archive is missing $path" >&2; exit 1; }
done

existing=
for path in $required etc/init.d/S01seedrng etc/init.d/S80dnsmasq; do
  [ ! -e "/$path" ] || existing="$existing $path"
done
[ -z "$existing" ] || tar -cf "$backup" -C / $existing

install_file() {
  path=$1
  mode=$2
  mkdir -p "/$(dirname "$path")"
  cp "$stage/$path" "/$path.new"
  chmod "$mode" "/$path.new"
  mv -f "/$path.new" "/$path"
}

for path in \
  boot/uEnv.txt \
  boot/uEnv-debug.txt \
  etc/fstab \
  etc/microfx.conf \
  etc/microfx-release \
  usr/share/microfx/onboarding.js; do
  install_file "$path" 0644
done

for path in \
  etc/init.d/S39seedrng \
  etc/init.d/S39data \
  etc/init.d/S40canvas \
  etc/init.d/S40provision \
  etc/init.d/S45status \
  usr/sbin/canvas-supervisor \
  usr/sbin/microfx-benchmark-override \
  usr/sbin/microfx-benchmark-capture \
  usr/sbin/microfx-status; do
  install_file "$path" 0755
done

# The development profile intentionally has no setup AP. Stop the old portal
# only after every replacement file is durable; client wlan1 and Dropbear are
# not touched by this transition.
/etc/init.d/S40provision stop || true
[ ! -x /etc/init.d/S80dnsmasq ] || /etc/init.d/S80dnsmasq stop || true
killall provision-watchdog 2>/dev/null || true
killall hostapd 2>/dev/null || true
killall dnsmasq 2>/dev/null || true
rm -f /etc/init.d/S01seedrng /etc/init.d/S80dnsmasq

/etc/init.d/S39seedrng start || true
/etc/init.d/S45status restart
/etc/init.d/S40canvas restart
sleep 4

ip route | grep -q '^default .* dev wlan1' || { echo "Wi-Fi route disappeared after update" >&2; exit 1; }
ps w | grep '[d]ropbear' >/dev/null || { echo "SSH disappeared after update" >&2; exit 1; }
ps w | grep '[c]anvas-supervisor' >/dev/null || { echo "Graphics supervisor did not restart" >&2; exit 1; }
ps w | grep '[c]anvas-demo' >/dev/null || { echo "Renderer did not restart" >&2; exit 1; }
ps w | grep '[h]ostapd\|[d]nsmasq\|[p]rovision-watchdog' >/dev/null && {
  echo "Provisioning process remained after disable" >&2
  exit 1
}

/usr/sbin/microfx-status > /run/microfx-status.new
mv -f /run/microfx-status.new /run/microfx-status
cat /run/microfx-status
echo "backup\t$backup"

rm -rf "$stage" "$archive" "$0"
