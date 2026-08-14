#!/bin/sh
set -eu

host="${1:-192.168.3.109}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ssh_wrapper="$script_dir/canvas-ssh.sh"

if [ ! -x "$ssh_wrapper" ]; then
    echo "Missing SSH wrapper: $ssh_wrapper" >&2
    exit 1
fi

"$ssh_wrapper" "$host" '
set -eu
echo "===== DRM devices ====="
ls -l /dev/dri 2>/dev/null || true

echo "===== connector status and modes ====="
for connector in /sys/class/drm/card*-*; do
    [ -d "$connector" ] || continue
    echo "--- $connector"
    [ -r "$connector/status" ] && cat "$connector/status"
    [ -r "$connector/modes" ] && cat "$connector/modes"
done

echo "===== plane inventory ====="
if command -v modetest >/dev/null 2>&1; then
    echo "===== modetest imx-drm connectors ====="
    modetest -M imx-drm -c 2>&1 || true
    echo "===== modetest imx-drm planes ====="
    modetest -M imx-drm -p 2>&1 || true
    echo "===== modetest etnaviv planes ====="
    modetest -M etnaviv -p 2>&1 || true
else
    echo "modetest is not installed in the development image."
    echo "Add the libdrm test tools only for the later hardware experiment."
fi
'
