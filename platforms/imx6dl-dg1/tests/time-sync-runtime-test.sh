#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
. "$REPO/tests/lib/microfx-test.sh"
OVERLAY=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay
SERVICE=$OVERLAY/etc/init.d/S42time
PEER_SERVICE=$OVERLAY/etc/init.d/S43peer-bridge
TEMP=$(microfx_test_tempdir)
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

mkdir -p "$TEMP/bin" "$TEMP/run" "$TEMP/data/state"
: >"$TEMP/release"
printf '%s\n' '/dev/mmcblk0p4 /data ext4 rw 0 0' >"$TEMP/mounts"
printf '%s\n' invalid >"$TEMP/clock"
: >"$TEMP/actions"

cat >"$TEMP/bin/date-mock" <<'EOF'
#!/bin/sh
case "$*" in
  '-u +%Y')
    value=$(cat "$MOCK_CLOCK")
    [ "$value" = invalid ] && echo 1970 || echo 2026
    ;;
  '-u -r '*'+%s') echo 1786680000 ;;
  '-u -s @'*)
    value=${3#@}
    printf '%s\n' "$value" >"$MOCK_CLOCK"
    echo "date-set $value" >>"$MOCK_ACTIONS"
    ;;
  '-u +%s')
    value=$(cat "$MOCK_CLOCK")
    [ "$value" = invalid ] && echo 0 || echo "$value"
    ;;
  '-u -Iseconds') echo '2026-08-14T08:00:00+00:00' ;;
  *) echo "unexpected date arguments: $*" >&2; exit 1 ;;
esac
EOF
cat >"$TEMP/bin/ip-mock" <<'EOF'
#!/bin/sh
[ "$*" = '-4 route show default' ] && echo 'default via 192.0.2.1 dev wlan1'
EOF
cat >"$TEMP/bin/timeout-mock" <<'EOF'
#!/bin/sh
echo "timeout $*" >>"$MOCK_ACTIONS"
exit 0
EOF
chmod +x "$TEMP/bin/date-mock" "$TEMP/bin/ip-mock" "$TEMP/bin/timeout-mock"

MOCK_CLOCK="$TEMP/clock" \
MOCK_ACTIONS="$TEMP/actions" \
MICROFX_RUN_ROOT="$TEMP/run" \
MICROFX_DATA_ROOT=/data \
MICROFX_MOUNTS_FILE="$TEMP/mounts" \
MICROFX_DATE_COMMAND="$TEMP/bin/date-mock" \
MICROFX_IP_COMMAND="$TEMP/bin/ip-mock" \
MICROFX_TIMEOUT_COMMAND="$TEMP/bin/timeout-mock" \
MICROFX_NTPD_COMMAND=ntpd-mock \
MICROFX_RELEASE_FILE="$TEMP/release" \
MICROFX_TIME_SEED_FILE="$TEMP/data/state/last-known-time" \
MICROFX_TIME_LOG="$TEMP/time.log" \
MICROFX_TIME_MAX_ATTEMPTS=1 \
MICROFX_TIME_RETRY_DELAY=0 \
MICROFX_TIME_SYNCED_RETRY_DELAY=0 \
  "$SERVICE" start

tries=0
while [ "$tries" -lt 50 ] && ! grep -q '^state[[:space:]]stopped$' "$TEMP/run/microfx-time-sync.status" 2>/dev/null; do
  tries=$((tries + 1))
  sleep 0.02
done

microfx_assert_file_contains '^1786680000$' "$TEMP/clock" "release timestamp did not seed the invalid clock"
microfx_assert_file_contains '^1786680000$' "$TEMP/data/state/last-known-time" "usable clock was not persisted once"
microfx_assert_file_contains '^date-set 1786680000$' "$TEMP/actions" "clock setter was not invoked"
microfx_assert_file_contains '^timeout 20 ntpd-mock -q ' "$TEMP/actions" "NTP retry was not attempted"
microfx_assert_file_contains '^NTP attempt 1$' "$TEMP/time.log" "NTP attempt was not logged"
microfx_assert_file_contains '^state[[:space:]]stopped$' "$TEMP/run/microfx-time-sync.status" "bounded test service did not finish"
microfx_assert_file_contains 'waiting for a TLS-usable system clock' "$PEER_SERVICE" "PeerJS is not gated on usable time"
microfx_assert_file_contains 'kill -0' "$PEER_SERVICE" "PeerJS init is not idempotent"

microfx_test_finish "clock seeding and persistent NTP retry tests"
