#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${1:-192.168.3.109}
SOURCE="$PLATFORM_DIR/experimental/kms-capability-probe.c"
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"
SSH_WRAPPER="$PLATFORM_DIR/scripts/canvas-ssh.sh"
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm

[ -r "$SOURCE" ] || { echo "Missing KMS probe source: $SOURCE" >&2; exit 1; }
[ -r "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }

WORK=$(mktemp -d /tmp/microfx-kms-probe.XXXXXX)
REMOTE_PROBE=/tmp/microfx-kms-capability-probe
cleanup() {
    "$SSH_WRAPPER" "$HOST" "rm -f $REMOTE_PROBE" >/dev/null 2>&1 || true
    rm -rf "$WORK"
    limactl shell "$VM_NAME" -- rm -f /tmp/microfx-kms-capability-probe.c \
        /tmp/microfx-kms-capability-probe >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

limactl copy --backend=scp "$SOURCE" "$VM_NAME:/tmp/microfx-kms-capability-probe.c"
limactl shell "$VM_NAME" -- sh -lc '
    set -eu
    out="$HOME/microfx-imx6dl-output"
    sysroot="$out/host/arm-buildroot-linux-gnueabihf/sysroot"
    cc="$out/host/bin/arm-buildroot-linux-gnueabihf-gcc"
    "$cc" --sysroot="$sysroot" -std=c11 -Wall -Wextra -Werror -O2 \
        -I"$sysroot/usr/include/libdrm" \
        /tmp/microfx-kms-capability-probe.c \
        -L"$sysroot/usr/lib" -ldrm \
        -o /tmp/microfx-kms-capability-probe
'
limactl copy --backend=scp \
    "$VM_NAME:/tmp/microfx-kms-capability-probe" \
    "$WORK/microfx-kms-capability-probe"
chmod 0755 "$WORK/microfx-kms-capability-probe"

scp -O -i "$KEY" -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 \
    -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
    "$WORK/microfx-kms-capability-probe" "root@$HOST:$REMOTE_PROBE"
"$SSH_WRAPPER" "$HOST" "chmod 0755 $REMOTE_PROBE && $REMOTE_PROBE /dev/dri/card1"
