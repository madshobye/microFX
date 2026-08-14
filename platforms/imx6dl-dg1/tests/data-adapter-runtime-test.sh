#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
adapter="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-data-adapters"
work=$(mktemp -d "${TMPDIR:-/tmp}/microfx-data-runtime.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

bin=$work/bin
runtime=$work/run/data
state=$work/run/state
projects=$work/projects
mock=$work/mock
mkdir -p "$bin" "$runtime" "$state" "$projects/flight-board" "$mock"
printf '%s\n' "$projects/flight-board" >"$mock/current-target"
printf '%s\n' 1700000000 >"$mock/epoch"
printf '%s\n' success >"$mock/curl-mode"
printf '%s\n' '{"source":"fixture"}' >"$mock/raw"
printf '%s\n' '{"schema":1,"version":1}' >"$mock/normalized"

cat >"$bin/readlink" <<'EOF'
#!/bin/sh
cat "$MOCK_STATE/current-target"
EOF
cat >"$bin/date" <<'EOF'
#!/bin/sh
cat "$MOCK_STATE/epoch"
EOF
cat >"$bin/curl" <<'EOF'
#!/bin/sh
count=0
[ ! -r "$MOCK_STATE/curl-count" ] || read -r count <"$MOCK_STATE/curl-count"
count=$((count + 1))
printf '%s\n' "$count" >"$MOCK_STATE/curl-count"
read -r mode <"$MOCK_STATE/curl-mode"
case "$mode" in
  success) cat "$MOCK_STATE/raw";;
  oversized) printf '%040d\n' 0;;
  fail) exit 22;;
esac
EOF
cat >"$bin/qjs" <<'EOF'
#!/bin/sh
cat "$MOCK_STATE/normalized"
EOF
cat >"$bin/reload" <<'EOF'
#!/bin/sh
printf '%s %s\n' "$1" "$2" >>"$MOCK_STATE/reloads"
EOF
chmod +x "$bin/readlink" "$bin/date" "$bin/curl" "$bin/qjs" "$bin/reload"

run_adapter() {
  env \
    PATH="$bin:$PATH" \
    MOCK_STATE="$mock" \
    MICROFX_CONFIG_FILE="$work/no-config" \
    MICROFX_DATA_ADAPTERS=1 \
    MICROFX_DATA_RUNTIME_ROOT="$runtime" \
    MICROFX_DATA_STATE_ROOT="$state" \
    MICROFX_PROJECTS_ROOT="$projects" \
    MICROFX_CURRENT_PROJECT_LINK="$work/current" \
    MICROFX_DATA_PID_FILE="$work/adapter.pid" \
    MICROFX_CURL_COMMAND="$bin/curl" \
    MICROFX_QJS_COMMAND="$bin/qjs" \
    MICROFX_RELOAD_COMMAND="$bin/reload" \
    MICROFX_DATE_COMMAND="$bin/date" \
    MICROFX_DATA_ONCE=1 \
    MICROFX_DATA_RETRY_SECONDS=30 \
    MICROFX_DATA_MAX_BYTES="${1:-262144}" \
    MICROFX_FLIGHT_REFRESH_SECONDS=300 \
    "$adapter"
}

assert_file_equals() {
  expected=$1
  file=$2
  actual=$(cat "$file")
  [ "$actual" = "$expected" ] || {
    echo "expected '$expected' in $file, got '$actual'" >&2
    exit 1
  }
}

# First success publishes atomically to RAM, records success, and reloads once.
run_adapter
assert_file_equals 1 "$mock/curl-count"
assert_file_equals '{"schema":1,"version":1}' "$runtime/flight-board/flights.json"
assert_file_equals 1700000000 "$state/flight-board.last-success"
grep -q '^state[[:space:]]fresh$' "$state/flight-board.status"
[ "$(wc -l <"$mock/reloads" | tr -d ' ')" -eq 1 ]

# A normal refresh is suppressed until its project interval expires.
run_adapter
assert_file_equals 1 "$mock/curl-count"

# A failed refresh retains last-good data and uses the shorter retry interval.
printf '%s\n' 1700000301 >"$mock/epoch"
printf '%s\n' fail >"$mock/curl-mode"
run_adapter
assert_file_equals 2 "$mock/curl-count"
assert_file_equals '{"schema":1,"version":1}' "$runtime/flight-board/flights.json"
assert_file_equals 1700000000 "$state/flight-board.last-success"
grep -q '^detail[[:space:]]fetch-failed$' "$state/flight-board.status"
printf '%s\n' 1700000310 >"$mock/epoch"
run_adapter
assert_file_equals 2 "$mock/curl-count"

# After retry delay, changed normalized data is published and reloaded.
printf '%s\n' 1700000332 >"$mock/epoch"
printf '%s\n' success >"$mock/curl-mode"
printf '%s\n' '{"schema":1,"version":2}' >"$mock/normalized"
run_adapter
assert_file_equals 3 "$mock/curl-count"
assert_file_equals '{"schema":1,"version":2}' "$runtime/flight-board/flights.json"
[ "$(wc -l <"$mock/reloads" | tr -d ' ')" -eq 2 ]

# An unsynchronized clock makes no TLS request and reports a RAM-only state.
rm -f "$state/flight-board.last-success" "$state/flight-board.last-attempt"
printf '%s\n' 100 >"$mock/epoch"
run_adapter
assert_file_equals 3 "$mock/curl-count"
grep -q '^state[[:space:]]waiting-clock$' "$state/flight-board.status"

# Oversized responses are rejected without replacing the last-good asset.
printf '%s\n' 1700001000 >"$mock/epoch"
printf '%s\n' oversized >"$mock/curl-mode"
run_adapter 16
assert_file_equals 4 "$mock/curl-count"
assert_file_equals '{"schema":1,"version":2}' "$runtime/flight-board/flights.json"
grep -q '^detail[[:space:]]response-too-large$' "$state/flight-board.status"

echo "data adapter runtime fixture passed"
