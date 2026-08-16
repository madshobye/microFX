#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUPERVISOR="$ROOT/platforms/imx6dl-dg1/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/canvas-supervisor"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-supervisor-test.XXXXXX")
supervisor_pid=
trap 'test -z "$supervisor_pid" || kill "$supervisor_pid" 2>/dev/null || true; rm -rf "$WORK"' EXIT

mkdir -p "$WORK/data/apps/current-runtime" "$WORK/data/apps/projects/demo/assets" \
  "$WORK/data/config" "$WORK/data/state" "$WORK/run"
printf '%s\n' '// firmware error app fixture' >"$WORK/error.js"
printf '%s\n' '// integration project' >"$WORK/data/apps/projects/demo/main.js"
ln -s "$WORK/data/apps/projects/demo" "$WORK/data/apps/current"

cat >"$WORK/data/apps/current-runtime/canvas-demo" <<'EOF'
#!/bin/sh
trap 'exit 0' TERM INT
if [ "${MICROFX_SCRIPT:-}" = "${MICROFX_ERROR_SCRIPT:-}" ]; then
  printf '%s\n' "error-screen:${MICROFX_ERROR_DETAIL:-missing}" >>"$MICROFX_DATA_ROOT/state/fake-renderer.log"
  while :; do sleep 1; done
fi
if [ -e "$MICROFX_DATA_ROOT/state/fail-next" ]; then
  rm -f "$MICROFX_DATA_ROOT/state/fail-next"
  echo 'MICROFX_JS_ERROR SyntaxError: demo/main.js:17: unexpected token' >&2
  exit 23
fi
printf '%s\n' "start:$MICROFX_DATA_ROOT" >>"$MICROFX_DATA_ROOT/state/fake-renderer.log"
printf 'benchmark:%s:%s:%s\n' "${MICROFX_OUTPUT_WIDTH:-unset}" \
  "${MICROFX_TARGET_FPS:-unset}" "${MICROFX_SCRIPT:-unset}" \
  >>"$MICROFX_DATA_ROOT/state/fake-renderer.log"
while :; do sleep 1; done
EOF
chmod +x "$WORK/data/apps/current-runtime/canvas-demo"

CANVAS_CONFIG="$WORK/missing-canvas.conf" \
MICROFX_PRODUCT_CONFIG="$WORK/missing-product.conf" \
MICROFX_DATA_ROOT="$WORK/data" \
MICROFX_RUN_ROOT="$WORK/run" \
MICROFX_RUNTIME_APP="$WORK/data/apps/current-runtime/canvas-demo" \
MICROFX_ONBOARDING_SCRIPT="$WORK/missing-onboarding.js" \
MICROFX_ERROR_SCRIPT="$WORK/error.js" \
MICROFX_REQUIRE_DATA_MOUNT=0 \
MICROFX_HEALTH_SECONDS=1 \
CANVAS_FAIL_FAST=1 \
CANVAS_PERSIST_LOGS=1 \
  "$SUPERVISOR" &
supervisor_pid=$!

wait_for() {
  description=$1
  shift
  attempts=0
  while ! "$@"; do
    attempts=$((attempts + 1))
    if [ "$attempts" -ge 80 ]; then
      echo "Timed out waiting for $description" >&2
      cat "$WORK/run/microfx-project-status" 2>/dev/null || true
      cat "$WORK/data/state/canvas.log" 2>/dev/null || true
      exit 1
    fi
    sleep 0.1
  done
}

has_started() { [ -s "$WORK/data/state/fake-renderer.log" ]; }
status_is() { [ -r "$WORK/run/microfx-project-status" ] && grep -q "^$1.demo.$2" "$WORK/run/microfx-project-status"; }
error_is_visible() { grep -q '^error-screen:SyntaxError: demo/main.js:17: unexpected token' "$WORK/data/state/fake-renderer.log"; }

wait_for "initial renderer" has_started
cat >"$WORK/run/microfx-benchmark.env" <<'EOF'
MICROFX_OUTPUT_WIDTH=1280
MICROFX_TARGET_FPS=30;touch-bad
MICROFX_SCRIPT=/tmp/not-allowed.js
EOF
printf 'save-run-1\tdemo\n' >"$WORK/run/microfx-project-reload"
wait_for "successful activation acknowledgement" status_is save-run-1 running
test ! -e "$WORK/run/microfx-project-reload"
grep -q '^benchmark:1280:unset:unset$' "$WORK/data/state/fake-renderer.log"
rm -f "$WORK/run/microfx-benchmark.env"

touch "$WORK/data/state/fail-next"
printf 'save-run-2\tdemo\n' >"$WORK/run/microfx-project-reload"
wait_for "failed activation acknowledgement" status_is save-run-2 failed
wait_for "project error screen" error_is_visible

# A failed script must not kill the supervisor or roll back implicitly. Studio
# can save corrected code and request another activation without a reboot.
printf 'save-run-3\tdemo\n' >"$WORK/run/microfx-project-reload"
wait_for "activation after a failed renderer" status_is save-run-3 running
test "$(grep -c '^start:' "$WORK/data/state/fake-renderer.log")" -ge 3
test "$(tail -n 1 "$WORK/data/state/fake-renderer.log")" = 'benchmark:unset:unset:unset'

kill "$supervisor_pid" 2>/dev/null || true
wait "$supervisor_pid" 2>/dev/null || true
supervisor_pid=
echo "supervisor Save & Run integration tests passed"
