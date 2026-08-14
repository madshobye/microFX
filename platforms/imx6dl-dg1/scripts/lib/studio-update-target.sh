#!/bin/sh
set -eu

archive=${1:-}
update_id=${2:-}
expected_digest=${3:-}
case "$update_id" in ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID" >&2; exit 1;; esac
[ -f "$archive" ] || { echo "Missing update archive" >&2; exit 1; }
[ -n "$expected_digest" ] || { echo "Missing archive checksum" >&2; exit 1; }
actual_digest=$(sha256sum "$archive" | sed 's/[[:space:]].*//')
[ "$actual_digest" = "$expected_digest" ] || { echo "Checksum mismatch" >&2; exit 1; }
grep -q ' /data ' /proc/mounts || { echo "/data is not mounted" >&2; exit 1; }

stage=/tmp/microfx-studio-update
backup_dir=/data/state/live-component-backups
backup=$backup_dir/$update_id.tar
rm -rf "$stage"
mkdir -p "$stage" "$backup_dir"
tar -xf "$archive" -C "$stage"
[ -x "$stage/usr/bin/microfx-peer-bridge" ] || { echo "Missing peer bridge" >&2; exit 1; }
[ -s "$stage/www/studio/app.js" ] || { echo "Missing Studio" >&2; exit 1; }

existing=
if [ -e /usr/bin/microfx-peer-bridge ]; then
  existing="$existing usr/bin/microfx-peer-bridge"
fi
if [ -e /www/studio ]; then
  existing="$existing www/studio"
fi
[ -z "$existing" ] || tar -cf "$backup" -C / $existing

/etc/init.d/S43peer-bridge stop || true
cp "$stage/usr/bin/microfx-peer-bridge" /usr/bin/microfx-peer-bridge.new
chmod 0755 /usr/bin/microfx-peer-bridge.new
mv -f /usr/bin/microfx-peer-bridge.new /usr/bin/microfx-peer-bridge
rm -rf /www/studio.new
cp -R "$stage/www/studio" /www/studio.new
if [ -e /www/studio ]; then
  rm -rf /www/studio.old
  mv /www/studio /www/studio.old
fi
mv /www/studio.new /www/studio
rm -rf /www/studio.old
/etc/init.d/S43peer-bridge start
sleep 3
if ! ps w | grep '[m]icrofx-peer-bridge' >/dev/null; then
  /etc/init.d/S43peer-bridge stop || true
  rm -f /usr/bin/microfx-peer-bridge
  rm -rf /www/studio
  [ -z "$existing" ] || tar -xf "$backup" -C /
  /etc/init.d/S43peer-bridge start || true
  echo "Peer bridge failed after update; restored backup $backup" >&2
  exit 1
fi

rm -rf "$stage" "$archive"
rm -f "$0"
echo "backup\t$backup"
