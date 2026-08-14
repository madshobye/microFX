#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${1:-192.168.3.109}
DURATION=${MICROFX_IPU_RUNTIME_SECONDS:-10}
DENSITY=${MICROFX_IPU_RUNTIME_DENSITY:-0.5}
REMOTE_DIR=/tmp/microfx-ipu-runtime-test
REMOTE_LOCK=/run/microfx-ipu-demo-used
PROJECT=/data/apps/projects/demo-scene/main.js
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"
SSH_WRAPPER="$PLATFORM_DIR/scripts/canvas-ssh.sh"
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm

case "$DURATION" in ''|*[!0-9]*) echo "Invalid test duration: $DURATION" >&2; exit 2 ;; esac
[ "$DURATION" -ge 1 ] && [ "$DURATION" -le 60 ] || {
    echo "Test duration must be between 1 and 60 seconds" >&2
    exit 2
}
case "$DENSITY" in
    0.25|0.5|0.50|0.75) ;;
    *) echo "Test density must be one of 0.25, 0.5, 0.50, or 0.75" >&2; exit 2 ;;
esac
TIMEOUT=$((DURATION + 5))

WORK=$(mktemp -d /tmp/microfx-ipu-runtime-test.XXXXXX)
CANVAS_STOPPED=0
cleanup() {
    "$SSH_WRAPPER" "$HOST" "rm -rf $REMOTE_DIR" >/dev/null 2>&1 || true
    if [ "$CANVAS_STOPPED" = 1 ]; then
        "$SSH_WRAPPER" "$HOST" '/etc/init.d/S40canvas start' >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT HUP INT TERM

"$PLATFORM_DIR/scripts/test-vm.sh"
GUEST_HOME=$(limactl shell "$VM_NAME" -- printenv HOME)
limactl copy --backend=scp \
    "$VM_NAME:$GUEST_HOME/microfx-imx6dl-output/build/microfx-demo-1.0.0/canvas-demo" \
    "$WORK/canvas-demo"
limactl copy --backend=scp \
    "$VM_NAME:$GUEST_HOME/microfx-imx6dl-output/build/raylib-drm-5.5/src/libraylib.so.5.5.0" \
    "$WORK/libraylib.so.550"
chmod 0755 "$WORK/canvas-demo"

"$SSH_WRAPPER" "$HOST" \
    "grep -q 'pixelDensity: 1.0' $PROJECT || { echo 'Refusing IPU test: persistent demo is not fixed at density 1.0' >&2; exit 1; }; test ! -e $REMOTE_LOCK || { echo 'Refusing IPU test: an IPU test already ran this boot; reboot first' >&2; exit 1; }"
"$SSH_WRAPPER" "$HOST" "mkdir -p $REMOTE_DIR"
scp -O -i "$KEY" -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 \
    -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
    "$WORK/canvas-demo" "$WORK/libraylib.so.550" "root@$HOST:$REMOTE_DIR/"
"$SSH_WRAPPER" "$HOST" "touch $REMOTE_LOCK"
"$SSH_WRAPPER" "$HOST" '/etc/init.d/S40canvas stop; killall -9 canvas-demo 2>/dev/null || true'
CANVAS_STOPPED=1
"$SSH_WRAPPER" "$HOST" \
    "chmod 0755 $REMOTE_DIR/canvas-demo; status=0; timeout -s TERM -k 2 $TIMEOUT env LD_LIBRARY_PATH=$REMOTE_DIR MICROFX_SCRIPT=$PROJECT MICROFX_PIXEL_DENSITY=$DENSITY MICROFX_TARGET_FPS=60 MICROFX_PROFILE=1 MICROFX_TEST_SECONDS=$DURATION $REMOTE_DIR/canvas-demo >/tmp/microfx-ipu-runtime-test.log 2>&1 || status=\$?; cat /tmp/microfx-ipu-runtime-test.log; exit \$status"
