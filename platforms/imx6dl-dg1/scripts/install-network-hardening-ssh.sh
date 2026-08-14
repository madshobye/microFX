#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLATFORM_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
OVERLAY=$PLATFORM_DIR/buildroot/board/imx6dl-dg1/rootfs-overlay
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
KEY=$PLATFORM_DIR/private/canvas_debug_ed25519
RELEASE=${MICROFX_NETWORK_UPDATE_ID:-network-hardening-$(date -u +%Y%m%d-%H%M%S)}
TARGET_INSTALLER=$SCRIPT_DIR/lib/network-hardening-update-target.sh

case "$RELEASE" in
  ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID: $RELEASE" >&2; exit 1 ;;
esac
[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
[ -x "$TARGET_INSTALLER" ] || { echo "Missing target installer: $TARGET_INSTALLER" >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/microfx-network-update.XXXXXX")
cleanup() { rm -rf "$work"; }
trap cleanup EXIT INT TERM

stage_file() {
  source=$1
  target=$2
  mkdir -p "$work/root/$(dirname "$target")"
  cp "$source" "$work/root/$target"
  chmod 0755 "$work/root/$target"
}

stage_file "$OVERLAY/etc/init.d/S39dropbear-debug" etc/init.d/S39dropbear-debug
stage_file "$OVERLAY/etc/init.d/S39recovery-client" etc/init.d/S39recovery-client
stage_file "$OVERLAY/etc/init.d/S40canvas" etc/init.d/S40canvas
stage_file "$OVERLAY/etc/init.d/S41wifi" etc/init.d/S41wifi
stage_file "$OVERLAY/usr/sbin/microfx-recovery-guardian" usr/sbin/microfx-recovery-guardian
stage_file "$OVERLAY/usr/sbin/microfx-recovery-client" usr/sbin/microfx-recovery-client
stage_file "$OVERLAY/usr/sbin/wifi-connect" usr/sbin/wifi-connect
stage_file "$OVERLAY/usr/sbin/wifi-watchdog" usr/sbin/wifi-watchdog

archive=$work/$RELEASE.tar
(cd "$work/root" && tar -cf "$archive" .)
digest=$(shasum -a 256 "$archive" | awk '{print $1}')

ssh_options="-i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"
remote_archive=/tmp/$RELEASE.tar
remote_installer=/tmp/microfx-network-hardening-installer.sh

scp -O $ssh_options "$archive" "$TARGET_INSTALLER" "root@$HOST:/tmp/"
ssh $ssh_options "root@$HOST" \
  "mv /tmp/network-hardening-update-target.sh '$remote_installer' && chmod 0755 '$remote_installer' && '$remote_installer' '$remote_archive' '$RELEASE' '$digest'"

echo "Installed $RELEASE on the active root at $HOST; live Wi-Fi and SSH were not restarted"
