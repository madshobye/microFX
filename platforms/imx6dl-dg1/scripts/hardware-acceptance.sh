#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
LABEL=${2:-sample-$(date +%Y%m%d-%H%M%S)}
OUTPUT_ROOT=${3:-$SCRIPT_DIR/../artifacts/hardware}

case "$LABEL" in
  *[!A-Za-z0-9._-]*|'')
    echo "Label may contain only letters, numbers, dot, underscore, and dash" >&2
    exit 2
    ;;
esac

mkdir -p "$OUTPUT_ROOT"
REPORT="$OUTPUT_ROOT/$LABEL.txt"
SUMMARY="$OUTPUT_ROOT/$LABEL.summary.txt"

"$SCRIPT_DIR/hardware-smoke.sh" "$HOST" "$REPORT" >/dev/null
if python3 "$SCRIPT_DIR/evaluate-hardware-smoke.py" "$REPORT" >"$SUMMARY"; then
  cat "$SUMMARY"
  echo "Hardware acceptance sample passed: $LABEL" >&2
else
  status=$?
  cat "$SUMMARY"
  echo "Hardware acceptance sample failed: $LABEL" >&2
  echo "Evidence remains in $REPORT and $SUMMARY" >&2
  exit "$status"
fi
