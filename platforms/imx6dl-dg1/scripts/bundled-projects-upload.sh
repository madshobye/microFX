#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}

# Update inactive bundled examples first. The active demo-scene is replaced
# last, which limits the visible renderer interruption to one transaction.
for directory in "$REPO_DIR"/apps/projects/*; do
  [ -d "$directory" ] || continue
  project=$(basename "$directory")
  "$SCRIPT_DIR/project-upload.sh" "$HOST" "$project" "$project"
done
"$SCRIPT_DIR/project-upload.sh" "$HOST" demo demo
"$SCRIPT_DIR/project-upload.sh" "$HOST" demo-scene demo

echo "Installed all bundled projects on $HOST; user projects were untouched"
