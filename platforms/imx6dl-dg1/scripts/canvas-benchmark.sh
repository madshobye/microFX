#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ENGINE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../../../engine" && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
OUTPUT=${2:-"$SCRIPT_DIR/../artifacts/hardware/benchmark-$(date +%Y%m%d-%H%M%S)"}
SSH_WRAPPER=${MICROFX_SSH_WRAPPER:-$SCRIPT_DIR/canvas-ssh.sh}
PROFILES=${MICROFX_BENCHMARK_PROFILES:-"native-fixed native-75 native-50 720-fixed"}
CAPTURE_SECONDS=${MICROFX_BENCHMARK_SECONDS:-20}
MAX_BUDGET=${MICROFX_BENCHMARK_MAX_BUDGET_USE:-}
MAX_OVER=${MICROFX_BENCHMARK_MAX_OVER_BUDGET:-}

[ -x "$SSH_WRAPPER" ] || { echo "Missing SSH wrapper: $SSH_WRAPPER" >&2; exit 1; }
case "$CAPTURE_SECONDS" in ''|*[!0-9]*) echo "MICROFX_BENCHMARK_SECONDS must be numeric" >&2; exit 2 ;; esac
mkdir -p "$OUTPUT"
: >"$OUTPUT/all.log"

for profile in $PROFILES; do
  case "$profile" in
    native-auto|native-fixed|native-75|native-50|720-fixed|720-half|1080-fixed|1080-fixed-60|1080-msaa|1080-msaa-60|1080-color|1080-quality) ;;
    *) echo "Unknown benchmark profile: $profile" >&2; exit 2 ;;
  esac
  echo "Capturing $profile for $CAPTURE_SECONDS seconds" >&2
  "$SSH_WRAPPER" "$HOST" /usr/sbin/microfx-benchmark-capture "$profile" "$CAPTURE_SECONDS" \
    >"$OUTPUT/$profile.log"
  grep -q 'MICROFX_PROFILE' "$OUTPUT/$profile.log" || {
    echo "$profile produced no renderer profile records" >&2
    exit 1
  }
  cat "$OUTPUT/$profile.log" >>"$OUTPUT/all.log"
  python3 "$ENGINE_DIR/tools/profile-report.py" --json \
    <"$OUTPUT/$profile.log" >"$OUTPUT/$profile.json"
  python3 "$ENGINE_DIR/tools/profile-report.py" \
    <"$OUTPUT/$profile.log" >"$OUTPUT/$profile.txt"
done

set -- --matrix
[ -z "$MAX_BUDGET" ] || set -- "$@" --max-budget-use "$MAX_BUDGET"
[ -z "$MAX_OVER" ] || set -- "$@" --max-over-budget "$MAX_OVER"
python3 "$ENGINE_DIR/tools/profile-report.py" "$@" \
  <"$OUTPUT/all.log" >"$OUTPUT/matrix.txt"
python3 "$ENGINE_DIR/tools/profile-report.py" --matrix --json \
  <"$OUTPUT/all.log" >"$OUTPUT/matrix.json"
cat "$OUTPUT/matrix.txt"
echo "Saved benchmark evidence to $OUTPUT" >&2
