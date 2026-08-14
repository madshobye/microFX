#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
. "$REPO/tests/lib/microfx-test.sh"
OVERLAY=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay
TEMP=$(microfx_test_tempdir)
supervisor_pid=
status_pid=
trap 'test -z "$supervisor_pid" || kill "$supervisor_pid" 2>/dev/null || true; test -z "$status_pid" || kill "$status_pid" 2>/dev/null || true; rm -rf "$TEMP"' EXIT HUP INT TERM

microfx_assert_file_contains 'loglevel=3' "$OVERLAY/boot/uEnv.txt" "production kernel log level"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
if grep -q 'ignore_loglevel' "$OVERLAY/boot/uEnv.txt"; then
  microfx_test_fail "production boot forces ignored kernel log levels"
fi
microfx_assert_file_contains 'loglevel=7 ignore_loglevel' "$OVERLAY/boot/uEnv-debug.txt" "debug boot verbosity"
microfx_assert_file_contains '/data.*nodev,nosuid' "$OVERLAY/etc/fstab" "persistent mount hardening"
microfx_assert_file_contains '/tmp.*nosuid,nodev' "$OVERLAY/etc/fstab" "temporary mount hardening"
microfx_assert_file_contains 'rm -f "${TARGET_DIR}/etc/init.d/S01seedrng"' "$ROOT/buildroot/board/imx6dl-dg1/post-build.sh" "root seed service removal"
microfx_assert_file_contains 'rm -f "${TARGET_DIR}/etc/init.d/S80dnsmasq"' "$ROOT/buildroot/board/imx6dl-dg1/post-build.sh" "generic dnsmasq service removal"
microfx_assert_file_contains 'MICROFX_PROVISIONING="${MICROFX_PROVISIONING:-0}"' "$OVERLAY/usr/sbin/canvas-supervisor" "onboarding provisioning policy"
microfx_assert_file_contains 'provisioningEnabled.*MICROFX_PROVISIONING' "$REPO/apps/onboarding/scripts/main.js" "onboarding setup-network guard"

mkdir -p "$TEMP/bin" "$TEMP/data/state" "$TEMP/run" "$TEMP/firmware/ath6k/AR6003/hw2.1.1"
printf '/dev/mmcblk0p4 /data ext4 rw 0 0\n' >"$TEMP/mounts"
cat >"$TEMP/bin/seedrng" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >"$MICROFX_SEED_ACTIONS"
EOF
chmod +x "$TEMP/bin/seedrng"
PATH="$TEMP/bin:$PATH" MICROFX_MOUNTS_FILE="$TEMP/mounts" \
MICROFX_SEED_DIR="$TEMP/data/state/seedrng" MICROFX_SEED_ACTIONS="$TEMP/seed-actions" \
  "$OVERLAY/etc/init.d/S39seedrng" start
microfx_assert_file_contains "--seed-dir=$TEMP/data/state/seedrng" "$TEMP/seed-actions" "persistent seed directory"

cat >"$TEMP/bin/supervisor" <<'EOF'
#!/bin/sh
trap 'exit 0' TERM INT
while :; do sleep 60; done
EOF
chmod +x "$TEMP/bin/supervisor"
MICROFX_RUN_ROOT="$TEMP/run" MICROFX_SUPERVISOR="$TEMP/bin/supervisor" \
  "$OVERLAY/etc/init.d/S40canvas" start
supervisor_pid=$(cat "$TEMP/run/canvas-supervisor.pid")
MICROFX_RUN_ROOT="$TEMP/run" MICROFX_SUPERVISOR="$TEMP/bin/supervisor" \
  "$OVERLAY/etc/init.d/S40canvas" start
microfx_assert_eq "$supervisor_pid" "$(cat "$TEMP/run/canvas-supervisor.pid")" "graphics supervisor duplicate start"

printf 'root=/dev/mmcblk0p3 rw\n' >"$TEMP/cmdline"
printf 'state\tconnected\n' >"$TEMP/run/microfx-network-status"
printf 'state\tusable\n' >"$TEMP/run/microfx-time-sync.status"
printf 'state\tnormal\n' >"$TEMP/run/microfx-recovery-status"
printf '7\n' >"$TEMP/data/state/boot-count"
printf '1\n' >"$TEMP/data/state/short-boot-count"
printf firmware >"$TEMP/firmware/ath6k/AR6003/hw2.1.1/fw-3.bin"
cat >"$TEMP/release" <<'EOF'
MICROFX_IMAGE_SCHEMA=1
MICROFX_PLATFORM=imx6dl-dg1
MICROFX_BOOT_MODEL=existing-ab
MICROFX_RELEASE_ID=test-release
EOF
printf 'MICROFX_PROVISIONING=0\n' >"$TEMP/runtime.conf"
printf 'MICROFX_WIFI_INTERFACE=wlan1\n' >"$TEMP/product.conf"
cat >"$TEMP/bin/ip" <<'EOF'
#!/bin/sh
printf '4: wlan1 inet 192.0.2.10/24 brd 192.0.2.255 scope global wlan1\n'
EOF
chmod +x "$TEMP/bin/ip"
MICROFX_RUN_ROOT="$TEMP/run" MICROFX_DATA_ROOT="$TEMP/data" \
MICROFX_RELEASE_FILE="$TEMP/release" MICROFX_RUNTIME_CONFIG="$TEMP/runtime.conf" \
MICROFX_PRODUCT_CONFIG="$TEMP/product.conf" MICROFX_CMDLINE_FILE="$TEMP/cmdline" \
MICROFX_MOUNTS_FILE="$TEMP/mounts" MICROFX_FIRMWARE_ROOT="$TEMP/firmware" \
MICROFX_IP_CMD="$TEMP/bin/ip" "$OVERLAY/usr/sbin/microfx-status" >"$TEMP/status"
microfx_assert_file_contains '^release_id[[:space:]]*test-release$' "$TEMP/status" "status release ID"
microfx_assert_file_contains '^active_slot[[:space:]]*b$' "$TEMP/status" "status active slot"
microfx_assert_file_contains '^wifi_firmware[[:space:]]*fw-3.bin$' "$TEMP/status" "status Wi-Fi firmware"
microfx_assert_file_contains '^recovery_state[[:space:]]*normal$' "$TEMP/status" "status recovery state"
microfx_assert_file_contains '^graphics_supervisor[[:space:]]*running$' "$TEMP/status" "status graphics process"

cat >"$TEMP/bin/status-command" <<'EOF'
#!/bin/sh
printf 'state\tok\n'
EOF
chmod +x "$TEMP/bin/status-command"
MICROFX_RUN_ROOT="$TEMP/run" MICROFX_STATUS_COMMAND="$TEMP/bin/status-command" MICROFX_STATUS_INTERVAL=60 \
  "$OVERLAY/etc/init.d/S45status" start
status_pid=$(cat "$TEMP/run/microfx-status.pid")
MICROFX_RUN_ROOT="$TEMP/run" MICROFX_STATUS_COMMAND="$TEMP/bin/status-command" MICROFX_STATUS_INTERVAL=60 \
  "$OVERLAY/etc/init.d/S45status" start
microfx_assert_eq "$status_pid" "$(cat "$TEMP/run/microfx-status.pid")" "status reporter duplicate start"
attempt=0
while [ ! -s "$TEMP/run/microfx-status" ] && [ "$attempt" -lt 20 ]; do
  attempt=$((attempt + 1))
  sleep 0.05
done
microfx_assert_file_contains '^state[[:space:]]*ok$' "$TEMP/run/microfx-status" "RAM status report"

MICROFX_RUN_ROOT="$TEMP/run" MICROFX_STATUS_COMMAND="$TEMP/bin/status-command" \
  "$OVERLAY/etc/init.d/S45status" stop
status_pid=
MICROFX_RUN_ROOT="$TEMP/run" MICROFX_SUPERVISOR="$TEMP/bin/supervisor" \
  "$OVERLAY/etc/init.d/S40canvas" stop
supervisor_pid=

microfx_test_finish "platform hardening"
