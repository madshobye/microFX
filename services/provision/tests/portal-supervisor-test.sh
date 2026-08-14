#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$TEST_DIR/../../.." && pwd)
SUPERVISOR="$REPO/platforms/imx6dl-dg1/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/canvas-supervisor"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-portal-e2e.XXXXXX")
supervisor_pid=
trap 'test -z "$supervisor_pid" || kill "$supervisor_pid" 2>/dev/null || true; rm -rf "$WORK"' EXIT

mkdir -p "$WORK/data/apps/projects/good/assets" "$WORK/data/apps/projects/broken/assets" \
  "$WORK/data/config" "$WORK/data/state" "$WORK/run"
printf '%s\n' 'fx.circle(20,30,8);' >"$WORK/data/apps/projects/good/main.js"
printf '%s\n' 'FAIL_RENDERER' >"$WORK/data/apps/projects/broken/main.js"
printf '%s\n' '{"title":"Working project"}' >"$WORK/data/apps/projects/good/project.json"
printf '%s\n' '{"title":"Broken project"}' >"$WORK/data/apps/projects/broken/project.json"

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
  -DMICROFX_APPS_DIR="\"$WORK/data/apps\"" \
  -DMICROFX_RELOAD_SIGNAL="\"$WORK/run/microfx-project-reload\"" \
  -DMICROFX_RELOAD_STATUS="\"$WORK/run/microfx-project-status\"" \
  -DMICROFX_NETWORK_STATUS="\"$WORK/run/microfx-network-status\"" \
  -DMICROFX_PROVISION_STATUS="\"$WORK/run/microfx-provision-status\"" \
  "$TEST_DIR/../src/control.c" -o "$WORK/control"

printf 'state\thealthy\nradio\t1\nap_mode\t1\nlink\t1\naddress\t1\nportal\t1\nbeacon\t1\nfailures\t0\n' \
  >"$WORK/run/microfx-provision-status"

cat >"$WORK/renderer" <<'EOF'
#!/bin/sh
set -eu
code=$(cat "$MICROFX_DATA_ROOT/apps/current/main.js")
case "$code" in
  *FAIL_RENDERER*) echo "portal renderer rejected FAIL_RENDERER" >&2; exit 23 ;;
esac
printf 'start:%s\n' "$code" >>"$MICROFX_DATA_ROOT/state/portal-renderer.log"
trap 'exit 0' TERM INT
while :; do sleep 1; done
EOF
chmod +x "$WORK/renderer"

CANVAS_CONFIG="$WORK/missing-canvas.conf" \
MICROFX_PRODUCT_CONFIG="$WORK/missing-product.conf" \
MICROFX_DATA_ROOT="$WORK/data" \
MICROFX_RUN_ROOT="$WORK/run" \
MICROFX_FACTORY_APP="$WORK/renderer" \
MICROFX_ONBOARDING_SCRIPT="$WORK/missing-onboarding.js" \
MICROFX_REQUIRE_DATA_MOUNT=0 \
MICROFX_HEALTH_SECONDS=1 \
CANVAS_FAIL_FAST=1 \
  "$SUPERVISOR" &
supervisor_pid=$!

post() {
  body=$1
  printf '%s' "$body" | env REQUEST_METHOD=POST CONTENT_LENGTH="${#body}" "$WORK/control"
}

wait_status() {
  token=$1 expected=$2 attempts=0
  while :; do
    output=$(env REQUEST_METHOD=GET "$WORK/control")
    printf '%s' "$output" | grep -q "\"token\":\"$token\"" &&
      printf '%s' "$output" | grep -q "\"state\":\"$expected\"" && return 0
    attempts=$((attempts + 1))
    if [ "$attempts" -ge 80 ]; then
      echo "Timed out waiting for portal activation $token -> $expected" >&2
      printf '%s\n' "$output" >&2
      cat "$WORK/run/microfx-project-status" 2>/dev/null >&2 || true
      exit 1
    fi
    sleep 0.1
  done
}

response=$(post 'action=activate&project=good')
printf '%s' "$response" | grep -q '"ok":true'
token=$(printf '%s' "$response" | sed -n 's/.*"activation":"\([^"]*\)".*/\1/p')
[ -n "$token" ]
wait_status "$token" running
grep -q 'fx.circle(20,30,8);' "$WORK/data/state/portal-renderer.log"
env REQUEST_METHOD=GET "$WORK/control" | grep -q '"setup":{"state":"healthy","radio":"1","apMode":"1","link":"1","address":"1","portal":"1","beacon":"1","failures":"0"}'

response=$(post 'action=activate&project=broken')
token=$(printf '%s' "$response" | sed -n 's/.*"activation":"\([^"]*\)".*/\1/p')
[ -n "$token" ]
wait_status "$token" failed
env REQUEST_METHOD=GET "$WORK/control" | grep -q 'portal renderer rejected FAIL_RENDERER'

# The portal must recover the fail-fast supervisor without rebooting.
response=$(post 'action=activate&project=good')
token=$(printf '%s' "$response" | sed -n 's/.*"activation":"\([^"]*\)".*/\1/p')
wait_status "$token" running

# Restart uses the current project and must run through the same acknowledged path.
response=$(post 'action=restart')
token=$(printf '%s' "$response" | sed -n 's/.*"activation":"\([^"]*\)".*/\1/p')
wait_status "$token" running

kill "$supervisor_pid" 2>/dev/null || true
wait "$supervisor_pid" 2>/dev/null || true
supervisor_pid=
echo "portal-to-supervisor integration tests passed"
