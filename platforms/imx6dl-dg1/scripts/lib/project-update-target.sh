#!/bin/sh
set -eu

archive=${1:-}
project=${2:-}
update_id=${3:-}
expected_digest=${4:-}

case "$project" in ''|*[!A-Za-z0-9._-]*) echo "Invalid project ID" >&2; exit 1;; esac
case "$update_id" in ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID" >&2; exit 1;; esac
[ -f "$archive" ] || { echo "Missing update archive" >&2; exit 1; }
[ -n "$expected_digest" ] || { echo "Missing archive checksum" >&2; exit 1; }
actual_digest=$(sha256sum "$archive" | sed 's/[[:space:]].*//')
[ "$actual_digest" = "$expected_digest" ] || { echo "Checksum mismatch" >&2; exit 1; }
grep -q ' /data ' /proc/mounts || { echo "/data is not mounted" >&2; exit 1; }

apps_root=/data/apps
target=$apps_root/projects/$project
stage=$apps_root/projects/.$project.update.$$
active=0
log=/tmp/canvas.log
log_start=0
if [ -d "$target" ] && [ -L "$apps_root/current" ] &&
   [ "$(readlink -f "$apps_root/current")" = "$(readlink -f "$target")" ]; then
  active=1
fi

cleanup() { rm -rf "$stage"; }
trap cleanup EXIT INT TERM
mkdir -p "$stage"
tar -xf "$archive" -C "$stage"
[ -s "$stage/main.js" ] || { echo "Project has no main.js" >&2; exit 1; }
[ -s "$stage/project.json" ] || { echo "Project has no project.json" >&2; exit 1; }

if [ "$active" = 1 ]; then
  log_start=$(wc -l "$log" 2>/dev/null | awk '{print $1}')
  case "$log_start" in ''|*[!0-9]*) log_start=0;; esac
  /etc/init.d/S40canvas stop
fi
mkdir -p "$target"
rm -rf "$target/main.js" "$target/project.json" "$target/assets"
mv "$stage/main.js" "$target/main.js"
mv "$stage/project.json" "$target/project.json"
mv "$stage/assets" "$target/assets"
sync

if [ "$active" = 1 ]; then
  /etc/init.d/S40canvas start
  sleep 5
  if ! ps w | grep '[c]anvas-demo' >/dev/null ||
     sed -n "$((log_start + 1)),\$p" "$log" 2>/dev/null |
       grep -E 'MICROFX_JS_ERROR|[Ff]atal|fail-fast supervisor' >/dev/null; then
    /etc/init.d/S40canvas stop || true
    echo "New project failed; rollback is disabled and the renderer remains stopped" >&2
    exit 1
  fi
fi

rm -f "$archive" "$0"
echo "project\t$project"
