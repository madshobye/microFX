#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLATFORM_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
OVERLAY=$PLATFORM_DIR/buildroot/board/imx6dl-dg1/rootfs-overlay
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
KEY=$PLATFORM_DIR/private/canvas_debug_ed25519
RELEASE=${MICROFX_ROOT_UPDATE_ID:-root-update-$(date -u +%Y%m%d-%H%M%S)}
TARGET_INSTALLER=$SCRIPT_DIR/lib/active-root-update-target.sh

case "$RELEASE" in
  ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID: $RELEASE" >&2; exit 1 ;;
esac

[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
[ -x "$TARGET_INSTALLER" ] || { echo "Missing target installer: $TARGET_INSTALLER" >&2; exit 1; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-root-update.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

stage_file() {
  source=$1
  target=$2
  mode=$3
  mkdir -p "$WORK/root/$(dirname "$target")"
  cp "$source" "$WORK/root/$target"
  chmod "$mode" "$WORK/root/$target"
}

stage_file "$OVERLAY/boot/uEnv.txt" boot/uEnv.txt 0644
stage_file "$OVERLAY/boot/uEnv-debug.txt" boot/uEnv-debug.txt 0644
stage_file "$OVERLAY/etc/fstab" etc/fstab 0644
stage_file "$OVERLAY/etc/microfx.conf" etc/microfx.conf 0644
stage_file "$OVERLAY/etc/microfx-release" etc/microfx-release 0644
stage_file "$OVERLAY/etc/init.d/S39seedrng" etc/init.d/S39seedrng 0755
stage_file "$OVERLAY/etc/init.d/S40canvas" etc/init.d/S40canvas 0755
stage_file "$OVERLAY/etc/init.d/S40provision" etc/init.d/S40provision 0755
stage_file "$OVERLAY/etc/init.d/S45status" etc/init.d/S45status 0755
stage_file "$OVERLAY/usr/sbin/canvas-supervisor" usr/sbin/canvas-supervisor 0755
stage_file "$OVERLAY/usr/sbin/microfx-benchmark-override" usr/sbin/microfx-benchmark-override 0755
stage_file "$OVERLAY/usr/sbin/microfx-benchmark-capture" usr/sbin/microfx-benchmark-capture 0755
stage_file "$OVERLAY/usr/sbin/microfx-status" usr/sbin/microfx-status 0755
stage_file "$REPO_DIR/apps/onboarding/scripts/main.js" usr/share/microfx/onboarding.js 0644

ARCHIVE=$WORK/$RELEASE.tar
(cd "$WORK/root" && tar -cf "$ARCHIVE" .)
DIGEST=$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')

SSH_OPTIONS="-i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"
REMOTE_ARCHIVE=/tmp/$RELEASE.tar
REMOTE_INSTALLER=/tmp/microfx-active-root-installer.sh

scp -O $SSH_OPTIONS "$ARCHIVE" "$TARGET_INSTALLER" "root@$HOST:/tmp/"
ssh $SSH_OPTIONS "root@$HOST" \
  "mv /tmp/active-root-update-target.sh '$REMOTE_INSTALLER' && chmod 0755 '$REMOTE_INSTALLER' && '$REMOTE_INSTALLER' '$REMOTE_ARCHIVE' '$RELEASE' '$DIGEST'"

echo "Installed $RELEASE on the active root at $HOST (no reboot)"
