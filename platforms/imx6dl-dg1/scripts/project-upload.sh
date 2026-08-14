#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLATFORM_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
PROJECT=${2:-demo-scene}
SOURCE=${3:-demo}
KEY=$PLATFORM_DIR/private/canvas_debug_ed25519
UPDATE_ID=${MICROFX_PROJECT_UPDATE_ID:-project-update-$(date -u +%Y%m%d-%H%M%S)}
TARGET_INSTALLER=$SCRIPT_DIR/lib/project-update-target.sh

case "$PROJECT" in
  ''|*[!A-Za-z0-9._-]*) echo "Invalid project ID: $PROJECT" >&2; exit 1 ;;
esac
case "$SOURCE" in
  demo)
    APP_DIR=$REPO_DIR/apps/demo
    MAIN_JS=$APP_DIR/scripts/main.js
    ;;
  ''|*[!A-Za-z0-9._-]*) echo "Invalid source project: $SOURCE" >&2; exit 1 ;;
  *)
    APP_DIR=$REPO_DIR/apps/projects/$SOURCE
    MAIN_JS=$APP_DIR/main.js
    ;;
esac
[ -s "$MAIN_JS" ] || { echo "Missing source main.js for $SOURCE" >&2; exit 1; }
[ -s "$APP_DIR/project.json" ] || { echo "Missing source project.json for $SOURCE" >&2; exit 1; }
case "$UPDATE_ID" in
  ''|*[!A-Za-z0-9._-]*) echo "Invalid update ID: $UPDATE_ID" >&2; exit 1 ;;
esac
[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
[ -x "$TARGET_INSTALLER" ] || { echo "Missing target installer: $TARGET_INSTALLER" >&2; exit 1; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-project-update.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

mkdir -p "$WORK/project"
cp "$MAIN_JS" "$WORK/project/main.js"
cp "$APP_DIR/project.json" "$WORK/project/project.json"
if [ -d "$APP_DIR/assets" ]; then
  cp -R "$APP_DIR/assets" "$WORK/project/assets"
else
  mkdir "$WORK/project/assets"
fi
ARCHIVE=$WORK/$UPDATE_ID.tar
(cd "$WORK/project" && tar -cf "$ARCHIVE" main.js project.json assets)
DIGEST=$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')

SSH_OPTIONS="-i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"
REMOTE_ARCHIVE=/tmp/$UPDATE_ID.tar
REMOTE_INSTALLER=/tmp/microfx-project-installer.sh

scp -O $SSH_OPTIONS "$ARCHIVE" "$TARGET_INSTALLER" "root@$HOST:/tmp/"
ssh $SSH_OPTIONS "root@$HOST" \
  "mv /tmp/project-update-target.sh '$REMOTE_INSTALLER' && chmod 0755 '$REMOTE_INSTALLER' && '$REMOTE_INSTALLER' '$REMOTE_ARCHIVE' '$PROJECT' '$UPDATE_ID' '$DIGEST'"

echo "Installed current $SOURCE source as project $PROJECT on $HOST"
