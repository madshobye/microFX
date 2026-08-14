#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${1:-192.168.3.109}
DURATION=${MICROFX_IPU_DEMO_SECONDS:-10}
DENSITY=${MICROFX_IPU_DEMO_DENSITY:-0.5}
NEXT_DENSITY=${MICROFX_IPU_DEMO_NEXT_DENSITY:-0}
SOURCE="$PLATFORM_DIR/experimental/ipu-scaling-demo.c"
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"
SSH_WRAPPER="$PLATFORM_DIR/scripts/canvas-ssh.sh"
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm

[ -r "$SOURCE" ] || { echo "Missing IPU demo source: $SOURCE" >&2; exit 1; }
[ -r "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
case "$DURATION" in ''|*[!0-9]*) echo "Invalid demo duration: $DURATION" >&2; exit 2 ;; esac
[ "$DURATION" -ge 1 ] && [ "$DURATION" -le 60 ] || {
    echo "Demo duration must be between 1 and 60 seconds" >&2
    exit 2
}
TIMEOUT=$((DURATION + 5))

WORK=$(mktemp -d /tmp/microfx-ipu-demo.XXXXXX)
REMOTE_DEMO=/tmp/microfx-ipu-scaling-demo
REMOTE_BOOT_LOCK=/run/microfx-ipu-demo-used
CANVAS_STOPPED=0
cleanup() {
    "$SSH_WRAPPER" "$HOST" "rm -f $REMOTE_DEMO" >/dev/null 2>&1 || true
    if [ "$CANVAS_STOPPED" = 1 ]; then
        "$SSH_WRAPPER" "$HOST" '/etc/init.d/S40canvas start' >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK"
    limactl shell "$VM_NAME" -- rm -f /tmp/microfx-ipu-scaling-demo.c \
        /tmp/microfx-ipu-scaling-demo >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

limactl copy --backend=scp "$SOURCE" "$VM_NAME:/tmp/microfx-ipu-scaling-demo.c"
limactl shell "$VM_NAME" -- sh -lc '
    set -eu
    out="$HOME/microfx-imx6dl-output"
    sysroot="$out/host/arm-buildroot-linux-gnueabihf/sysroot"
    cc="$out/host/bin/arm-buildroot-linux-gnueabihf-gcc"
    "$cc" --sysroot="$sysroot" -std=c11 -Wall -Wextra -Werror -O2 \
        -I"$sysroot/usr/include/libdrm" \
        /tmp/microfx-ipu-scaling-demo.c \
        -L"$sysroot/usr/lib" -ldrm -lgbm -lEGL -lGLESv2 -lm \
        -o /tmp/microfx-ipu-scaling-demo
'
limactl copy --backend=scp "$VM_NAME:/tmp/microfx-ipu-scaling-demo" \
    "$WORK/microfx-ipu-scaling-demo"
chmod 0755 "$WORK/microfx-ipu-scaling-demo"

scp -O -i "$KEY" -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 \
    -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
    "$WORK/microfx-ipu-scaling-demo" "root@$HOST:$REMOTE_DEMO"
"$SSH_WRAPPER" "$HOST" \
    'grep -q "pixelDensity: 1.0" /data/apps/projects/demo-scene/main.js || { echo "Refusing IPU test: persistent demo is not fixed at density 1.0" >&2; exit 1; }'
"$SSH_WRAPPER" "$HOST" \
    "test ! -e $REMOTE_BOOT_LOCK || { echo 'Refusing IPU test: an IPU test already ran this boot; reboot first' >&2; exit 1; }; touch $REMOTE_BOOT_LOCK"
"$SSH_WRAPPER" "$HOST" '/etc/init.d/S40canvas stop; killall -9 canvas-demo 2>/dev/null || true'
CANVAS_STOPPED=1
"$SSH_WRAPPER" "$HOST" \
    "chmod 0755 $REMOTE_DEMO && timeout -s TERM -k 2 $TIMEOUT $REMOTE_DEMO /dev/dri/card1 $DURATION $DENSITY $NEXT_DENSITY"
