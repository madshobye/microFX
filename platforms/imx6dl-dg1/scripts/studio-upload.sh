#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLATFORM_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
KEY=$PLATFORM_DIR/private/canvas_debug_ed25519
UPDATE_ID=${MICROFX_STUDIO_UPDATE_ID:-studio-update-$(date -u +%Y%m%d-%H%M%S)}
TARGET_INSTALLER=$SCRIPT_DIR/lib/studio-update-target.sh
. "$SCRIPT_DIR/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm

case "$UPDATE_ID" in ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID" >&2; exit 1;; esac
[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
[ -x "$TARGET_INSTALLER" ] || { echo "Missing target installer: $TARGET_INSTALLER" >&2; exit 1; }

"$SCRIPT_DIR/build-studio.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-studio-update.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT INT TERM
GUEST_HOME=$(limactl shell "$VM_NAME" -- printenv HOME)
GUEST_ARCHIVE=$GUEST_HOME/microfx-imx6dl-output/$UPDATE_ID.tar
limactl shell "$VM_NAME" -- test -x \
  "$GUEST_HOME/microfx-imx6dl-output/target/usr/bin/microfx-peer-bridge"
limactl shell "$VM_NAME" -- test -f \
  "$GUEST_HOME/microfx-imx6dl-output/target/www/studio/app.js"
limactl shell "$VM_NAME" -- tar -cf "$GUEST_ARCHIVE" \
  -C "$GUEST_HOME/microfx-imx6dl-output/target" usr/bin/microfx-peer-bridge www/studio
limactl copy --backend=scp "$VM_NAME:$GUEST_ARCHIVE" "$WORK/$UPDATE_ID.tar"
limactl shell "$VM_NAME" -- rm -f "$GUEST_ARCHIVE"
DIGEST=$(shasum -a 256 "$WORK/$UPDATE_ID.tar" | awk '{print $1}')

SSH_OPTIONS="-i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"
scp -O $SSH_OPTIONS "$WORK/$UPDATE_ID.tar" "$TARGET_INSTALLER" "root@$HOST:/tmp/"
ssh $SSH_OPTIONS "root@$HOST" \
  "mv /tmp/studio-update-target.sh /tmp/microfx-studio-installer.sh && chmod 0755 /tmp/microfx-studio-installer.sh && /tmp/microfx-studio-installer.sh '/tmp/$UPDATE_ID.tar' '$UPDATE_ID' '$DIGEST'"

echo "Installed current Studio and peer bridge on $HOST"
