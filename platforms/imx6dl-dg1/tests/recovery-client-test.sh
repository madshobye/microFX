#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
. "$REPO/tests/lib/microfx-test.sh"
OVERLAY=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay
TEMP=$(microfx_test_tempdir)
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

mkdir -p "$TEMP/bin" "$TEMP/run" "$TEMP/sys/wlan0" "$TEMP/sys/wlan1"
printf '%s\n' 'network={' '  ssid="stored"' '}' >"$TEMP/wpa.conf"
printf '%s\n' 'CANVAS_SSH=1' >"$TEMP/canvas.conf"
printf '%s\n' 'CANVAS_DEBUG=1' >>"$TEMP/canvas.conf"
: >"$TEMP/actions"

cat >"$TEMP/bin/wifi-service" <<'EOF'
#!/bin/sh
printf 'wifi-service %s\n' "$*" >>"$MOCK_ACTIONS"
EOF
chmod +x "$TEMP/bin/wifi-service"

cat >"$TEMP/bin/recovery-mock" <<'EOF'
#!/bin/sh
name=${0##*/}
case "$name" in
  sleep) exit 0 ;;
  date) echo 'Thu Aug 13 00:00:00 UTC 2026'; exit 0 ;;
esac
printf '%s %s\n' "$name" "$*" >>"$MOCK_ACTIONS"
case "$name $*" in
  'wpa_cli -i wlan1 status') echo 'wpa_state=COMPLETED' ;;
  'ip -4 address show dev wlan1') echo '    inet 192.0.2.44/24 scope global wlan1' ;;
  'ip -4 address show dev wlan0') : ;;
esac
exit 0
EOF
chmod +x "$TEMP/bin/recovery-mock"
for name in ip iw wpa_supplicant wpa_cli udhcpc killall sleep date ssh-service; do
  ln -s recovery-mock "$TEMP/bin/$name"
done

PATH="$TEMP/bin:$PATH" \
MOCK_ACTIONS="$TEMP/actions" \
MICROFX_RUNTIME_CONFIG="$TEMP/canvas.conf" \
MICROFX_PRODUCT_CONFIG="$TEMP/missing-product.conf" \
MICROFX_RUN_ROOT="$TEMP/run" \
MICROFX_SYS_CLASS_NET="$TEMP/sys" \
MICROFX_RECOVERY_CONFIG="$TEMP/wpa.conf" \
MICROFX_RECOVERY_ASSOCIATION_SECONDS=1 \
MICROFX_RECOVERY_ADDRESS_SECONDS=1 \
MICROFX_RECOVERY_MAX_ROUNDS=1 \
MICROFX_SSH_SERVICE="$TEMP/bin/ssh-service" \
MICROFX_RECOVERY_LOG="$TEMP/client.log" \
  "$OVERLAY/usr/sbin/microfx-recovery-client"

microfx_assert_file_contains '^wpa_supplicant .* -i wlan1 ' "$TEMP/actions" "known-working wlan1 sequence was not attempted"
if grep -q '^wpa_supplicant .* -i wlan0 ' "$TEMP/actions"; then
  microfx_test_fail "recovery used wlan0 while wlan1 existed"
fi
microfx_assert_file_contains '^state[[:space:]]connected$' "$TEMP/run/microfx-recovery-status" "recovery did not connect"
microfx_assert_file_contains '^interface[[:space:]]wlan1$' "$TEMP/run/microfx-recovery-status" "wrong recovery interface"
microfx_assert_file_contains '^address[[:space:]]192.0.2.44/24$' "$TEMP/run/microfx-recovery-status" "recovery address missing"

# A healthy default-route plus live Dropbear must never invoke recovery.
mkdir -p "$TEMP/guardian-run"
printf '%s\n' "$$" >"$TEMP/guardian-run/dropbear-debug.pid"
: >"$TEMP/guardian-actions"
cat >"$TEMP/bin/guardian-ip" <<'EOF'
#!/bin/sh
case "$*" in
  '-4 route show default') echo 'default via 192.0.2.1 dev wlan1' ;;
  '-4 address show dev wlan1') echo '    inet 192.0.2.44/24 scope global wlan1' ;;
esac
EOF
chmod +x "$TEMP/bin/guardian-ip"
ln -sf guardian-ip "$TEMP/bin/ip"
cat >"$TEMP/bin/should-not-run" <<'EOF'
#!/bin/sh
echo invoked >>"$MOCK_GUARDIAN_ACTIONS"
exit 1
EOF
chmod +x "$TEMP/bin/should-not-run"

PATH="$TEMP/bin:$PATH" \
MOCK_GUARDIAN_ACTIONS="$TEMP/guardian-actions" \
MOCK_ACTIONS="$TEMP/guardian-actions" \
MICROFX_RUNTIME_CONFIG="$TEMP/canvas.conf" \
MICROFX_RUN_ROOT="$TEMP/guardian-run" \
MICROFX_RECOVERY_INITIAL_DELAY=0 \
MICROFX_RECOVERY_POLL_DELAY=0 \
MICROFX_RECOVERY_MAX_CYCLES=1 \
MICROFX_RECOVERY_DISABLE_BOOT_GUARD=1 \
MICROFX_RECOVERY_CLIENT="$TEMP/bin/should-not-run" \
MICROFX_SSH_SERVICE="$TEMP/bin/should-not-run" \
MICROFX_RECOVERY_GUARDIAN_LOG="$TEMP/guardian.log" \
  "$OVERLAY/usr/sbin/microfx-recovery-guardian"
microfx_assert_file_empty "$TEMP/guardian-actions" "healthy LAN path invoked recovery"
microfx_assert_file_contains '^state[[:space:]]normal$' "$TEMP/guardian-run/microfx-recovery-status" "healthy recovery status"

# A missing partition 4 must not turn /data/state into a directory on the root
# slot merely to keep debug boot counters.
: >"$TEMP/no-data-mounts"
rm -rf "$TEMP/no-data-state"
PATH="$TEMP/bin:$PATH" \
MOCK_GUARDIAN_ACTIONS="$TEMP/guardian-actions" \
MOCK_ACTIONS="$TEMP/guardian-actions" \
MICROFX_RUNTIME_CONFIG="$TEMP/canvas.conf" \
MICROFX_RUN_ROOT="$TEMP/guardian-run" \
MICROFX_RECOVERY_STATE_ROOT="$TEMP/no-data-state" \
MICROFX_MOUNTS_FILE="$TEMP/no-data-mounts" \
MICROFX_RECOVERY_INITIAL_DELAY=0 \
MICROFX_RECOVERY_POLL_DELAY=0 \
MICROFX_RECOVERY_MAX_CYCLES=1 \
MICROFX_RECOVERY_CLIENT="$TEMP/bin/should-not-run" \
MICROFX_SSH_SERVICE="$TEMP/bin/should-not-run" \
MICROFX_RECOVERY_GUARDIAN_LOG="$TEMP/no-data-guardian.log" \
  "$OVERLAY/usr/sbin/microfx-recovery-guardian"
if [ -e "$TEMP/no-data-state" ]; then
  microfx_test_fail "boot guard wrote state without a mounted data partition"
fi
microfx_assert_file_contains 'boot-loop accounting is disabled' "$TEMP/no-data-guardian.log" "missing data partition was not reported"

# A failed bounded fallback returns ownership to the ordinary Wi-Fi service.
mkdir -p "$TEMP/fallback-run"
printf '%s\n' "$$" >"$TEMP/fallback-run/dropbear-debug.pid"
: >"$TEMP/fallback-actions"
cat >"$TEMP/bin/no-network-ip" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$TEMP/bin/recovery-fails" <<'EOF'
#!/bin/sh
printf 'recovery attempted\n' >>"$MOCK_FALLBACK_ACTIONS"
: >"$MICROFX_RUN_ROOT/microfx-recovery-active"
exit 1
EOF
chmod +x "$TEMP/bin/no-network-ip" "$TEMP/bin/recovery-fails"
ln -sf no-network-ip "$TEMP/bin/ip"
PATH="$TEMP/bin:$PATH" \
MOCK_FALLBACK_ACTIONS="$TEMP/fallback-actions" \
MOCK_ACTIONS="$TEMP/fallback-actions" \
MICROFX_RUNTIME_CONFIG="$TEMP/canvas.conf" \
MICROFX_RUN_ROOT="$TEMP/fallback-run" \
MICROFX_RECOVERY_DISABLE_BOOT_GUARD=1 \
MICROFX_RECOVERY_INITIAL_DELAY=0 \
MICROFX_RECOVERY_POLL_DELAY=0 \
MICROFX_RECOVERY_FAILURE_LIMIT=1 \
MICROFX_RECOVERY_MAX_CYCLES=1 \
MICROFX_RECOVERY_CLIENT="$TEMP/bin/recovery-fails" \
MICROFX_WIFI_SERVICE="$TEMP/bin/wifi-service" \
MICROFX_RECOVERY_GUARDIAN_LOG="$TEMP/fallback.log" \
  "$OVERLAY/usr/sbin/microfx-recovery-guardian"
microfx_assert_file_contains '^recovery attempted$' "$TEMP/fallback-actions" "fallback recovery was not attempted"
microfx_assert_file_contains '^wifi-service restart$' "$TEMP/fallback-actions" "normal Wi-Fi did not regain ownership"
test ! -e "$TEMP/fallback-run/microfx-recovery-active" || microfx_test_fail "failed fallback retained radio ownership"

# Three consecutive pending boots suppress graphics, but retain the normal
# network grace period instead of immediately taking ownership of the radios.
mkdir -p "$TEMP/boot-loop-run" "$TEMP/boot-loop-state"
printf '%s\n' "$$" >"$TEMP/boot-loop-run/dropbear-debug.pid"
printf '%s\n' 12 >"$TEMP/boot-loop-state/boot-count"
printf '%s\n' 12 >"$TEMP/boot-loop-state/boot-pending"
printf '%s\n' 11 >"$TEMP/boot-loop-state/boot-stable"
printf '%s\n' 1 >"$TEMP/boot-loop-state/short-boot-count"
: >"$TEMP/boot-loop-actions"
cat >"$TEMP/bin/recovery-invoked" <<'EOF'
#!/bin/sh
echo invoked >>"$MOCK_BOOT_LOOP_ACTIONS"
exit 0
EOF
chmod +x "$TEMP/bin/recovery-invoked"
ln -sf guardian-ip "$TEMP/bin/ip"

PATH="$TEMP/bin:$PATH" \
MOCK_BOOT_LOOP_ACTIONS="$TEMP/boot-loop-actions" \
MOCK_ACTIONS="$TEMP/boot-loop-actions" \
MICROFX_RUNTIME_CONFIG="$TEMP/canvas.conf" \
MICROFX_RUN_ROOT="$TEMP/boot-loop-run" \
MICROFX_RECOVERY_STATE_ROOT="$TEMP/boot-loop-state" \
MICROFX_RECOVERY_REQUIRE_DATA_MOUNT=0 \
MICROFX_RECOVERY_STABLE_SECONDS=600 \
MICROFX_RECOVERY_DISABLE_STABLE_TIMER=1 \
MICROFX_RECOVERY_INITIAL_DELAY=0 \
MICROFX_RECOVERY_POLL_DELAY=0 \
MICROFX_RECOVERY_MAX_CYCLES=1 \
MICROFX_RECOVERY_CLIENT="$TEMP/bin/recovery-invoked" \
MICROFX_SSH_SERVICE="$TEMP/bin/should-not-run" \
  "$OVERLAY/usr/sbin/microfx-recovery-guardian"
microfx_assert_file_empty "$TEMP/boot-loop-actions" "short-boot protection took over healthy normal Wi-Fi"
microfx_assert_file_contains '^13$' "$TEMP/boot-loop-state/boot-count" "boot counter did not increment"
microfx_assert_file_contains '^2$' "$TEMP/boot-loop-state/short-boot-count" "short-boot counter did not increment"
test -e "$TEMP/boot-loop-run/microfx-graphics-safe-mode" || microfx_test_fail "short-boot protection did not suppress graphics"
microfx_assert_file_contains '^state[[:space:]]normal$' "$TEMP/boot-loop-run/microfx-recovery-status" "normal network did not recover in graphics safe mode"

microfx_test_finish "stored-network SSH recovery tests"
