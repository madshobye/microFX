#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLATFORM_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-hardware-smoke.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

cat >"$WORK/ssh" <<'EOF'
#!/bin/sh
set -eu
[ "$1" = "fixture.local" ]
for required in /proc/cmdline /etc/microfx-release cmdline_root /data/apps/current client_interface provision_interface hostapd_cli microfx-peer-bridge time_sync; do
  case "$2" in
    *"$required"*) ;;
    *) echo "collector omitted required evidence: $required" >&2; exit 1 ;;
  esac
done
cat "$MICROFX_SMOKE_FIXTURE"
EOF
chmod +x "$WORK/ssh"

MICROFX_SMOKE_FIXTURE="$TEST_DIR/fixtures/hardware-smoke-pass.txt" \
  MICROFX_SSH_WRAPPER="$WORK/ssh" \
  "$PLATFORM_DIR/scripts/hardware-smoke.sh" fixture.local "$WORK/report.txt" \
  >"$WORK/stdout.txt" 2>"$WORK/stderr.txt"

cmp "$WORK/report.txt" "$WORK/stdout.txt"
grep -q '^hostname=microfx$' "$WORK/report.txt"
grep -q '^wpa_state=COMPLETED$' "$WORK/report.txt"
grep -q 'Saved hardware smoke report' "$WORK/stderr.txt"

python3 "$PLATFORM_DIR/scripts/evaluate-hardware-smoke.py" "$WORK/report.txt" \
  >"$WORK/summary.txt"
grep -q '^PASS known A/B root slot$' "$WORK/summary.txt"
grep -q '^PASS current image schema$' "$WORK/summary.txt"
grep -q '^PASS current image platform$' "$WORK/summary.txt"
grep -q '^PASS authoritative boot model$' "$WORK/summary.txt"
grep -q '^PASS setup beacon enabled$' "$WORK/summary.txt"
grep -q '^PASS AR6003 board data loaded without fallback$' "$WORK/summary.txt"
grep -q '^PASS client and setup radios distinct$' "$WORK/summary.txt"
grep -q '^PASS time synchronization usable$' "$WORK/summary.txt"
grep -q '^RESULT pass checks=24 failed=0$' "$WORK/summary.txt"

sed 's/^renderer=running$/renderer=down/' "$WORK/report.txt" >"$WORK/failed.txt"
if python3 "$PLATFORM_DIR/scripts/evaluate-hardware-smoke.py" "$WORK/failed.txt" \
    >"$WORK/failed-summary.txt"; then
  echo "evaluator accepted a stopped renderer" >&2
  exit 1
fi
grep -q '^FAIL renderer running$' "$WORK/failed-summary.txt"

sed 's/^board_data_fallback_warnings=0$/board_data_fallback_warnings=2/' \
  "$WORK/report.txt" >"$WORK/firmware-failed.txt"
if python3 "$PLATFORM_DIR/scripts/evaluate-hardware-smoke.py" \
    "$WORK/firmware-failed.txt" >"$WORK/firmware-failed-summary.txt"; then
  echo "evaluator accepted AR6003 board-data fallback warnings" >&2
  exit 1
fi
grep -q '^FAIL AR6003 board data loaded without fallback$' \
  "$WORK/firmware-failed-summary.txt"

MICROFX_SMOKE_FIXTURE="$TEST_DIR/fixtures/hardware-smoke-pass.txt" \
  MICROFX_SSH_WRAPPER="$WORK/ssh" \
  "$PLATFORM_DIR/scripts/hardware-acceptance.sh" fixture.local near "$WORK/evidence" \
  >"$WORK/acceptance.stdout" 2>"$WORK/acceptance.stderr"
grep -q '^RESULT pass checks=24 failed=0$' "$WORK/acceptance.stdout"
grep -q 'Hardware acceptance sample passed: near' "$WORK/acceptance.stderr"
test -s "$WORK/evidence/near.txt"
test -s "$WORK/evidence/near.summary.txt"

echo "hardware smoke collector tests passed"
