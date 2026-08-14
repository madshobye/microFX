#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
. "$REPO/tests/lib/microfx-test.sh"
TESTS=$ROOT/tests
FIXTURES=$TESTS/fixtures
OVERLAY=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay
TEMP=$(microfx_test_tempdir)
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

make_mock_path() {
  directory=$1
  mkdir -p "$directory"
  for name in wpa_cli hostapd_cli ip iw wget udhcpc sleep date wifi-connect provision-service; do
    ln -s "$FIXTURES/watchdog-command-mock" "$directory/$name"
  done
}

run_wifi() {
  name=$1
  states=$2
  addresses=$3
  cycles=$4
  scenario=$TEMP/$name
  mkdir -p "$scenario/run"
  make_mock_path "$scenario/bin"
  printf '%b' "$states" >"$scenario/states"
  printf '%b' "$addresses" >"$scenario/addresses"
  : >"$scenario/actions"
  : >"$scenario/statuses"
  : >"$scenario/counter"
  PATH="$scenario/bin:$PATH" \
  MOCK_COUNTER="$scenario/counter" \
  MOCK_STATES="$scenario/states" \
  MOCK_ADDRESSES="$scenario/addresses" \
  MOCK_ACTIONS="$scenario/actions" \
  MOCK_STATUSES="$scenario/statuses" \
  MICROFX_RUN_ROOT="$scenario/run" \
  MICROFX_WIFI_POLICY_LIBRARY="$OVERLAY/usr/lib/microfx/wifi-policy.sh" \
  MICROFX_NETWORK_STATUS_LIBRARY="$FIXTURES/network-status-stub.sh" \
  MICROFX_WIFI_CONNECT="$scenario/bin/wifi-connect" \
  MICROFX_WIFI_WATCHDOG_LOG="$scenario/watchdog.log" \
  MICROFX_WIFI_WATCHDOG_INITIAL_DELAY=0 \
  MICROFX_WIFI_WATCHDOG_POLL_DELAY=0 \
  MICROFX_WIFI_WATCHDOG_REBUILD_DELAY=0 \
  MICROFX_WIFI_WATCHDOG_MAX_CYCLES="$cycles" \
    "$OVERLAY/usr/sbin/wifi-watchdog"
}

run_wifi association-failure \
  'DISCONNECTED\nDISCONNECTED\nDISCONNECTED\nDISCONNECTED\nDISCONNECTED\nDISCONNECTED\nDISCONNECTED\nDISCONNECTED\nDISCONNECTED\n' \
  '\n\n\n\n\n\n\n\n\n' 9
microfx_assert_file_count 1 '^reassociate$' "$TEMP/association-failure/actions" "reassociation threshold"
microfx_assert_file_count 1 '^rebuild$' "$TEMP/association-failure/actions" "rebuild threshold"
microfx_assert_file_count 9 '^reconnecting|' "$TEMP/association-failure/statuses" "reconnecting status"

run_wifi address-failure 'COMPLETED\nCOMPLETED\nCOMPLETED\n' '\n\n\n' 3
microfx_assert_file_contains '^renew-dhcp$' "$TEMP/address-failure/actions" "DHCP renewal threshold"
microfx_assert_file_contains '^awaiting-address|' "$TEMP/address-failure/statuses" "awaiting-address status"

run_wifi healthy 'COMPLETED\nCOMPLETED\nCOMPLETED\n' \
  '192.0.2.10/24\n192.0.2.10/24\n192.0.2.10/24\n' 3
microfx_assert_file_empty "$TEMP/healthy/actions" "healthy link triggered recovery"
microfx_assert_file_count 3 '^connected|' "$TEMP/healthy/statuses" "healthy status"

run_provision() {
  name=$1
  services=$2
  health=$3
  hostapd_state=${4:-}
  scenario=$TEMP/$name
  mkdir -p "$scenario/run" "$scenario/sys"
  make_mock_path "$scenario/bin"
  : >"$scenario/actions"
  if [ "$services" = 1 ]; then
    mkdir -p "$scenario/sys/ap0"
    for service in hostapd dnsmasq httpd; do
      printf '%s\n' "$$" >"$scenario/run/${service}-microfx.pid"
    done
  fi
  PATH="$scenario/bin:$PATH" \
  MOCK_ACTIONS="$scenario/actions" \
  MOCK_PROVISION_HEALTH="$health" \
  MOCK_HOSTAPD_STATE="$hostapd_state" \
  MICROFX_RUN_ROOT="$scenario/run" \
  MICROFX_SYS_CLASS_NET="$scenario/sys" \
  MICROFX_PROVISION_INTERFACE=ap0 \
  MICROFX_PROVISION_POLICY_LIBRARY="$OVERLAY/usr/lib/microfx/provision-policy.sh" \
  MICROFX_PROVISION_HEALTH_LIBRARY="$OVERLAY/usr/lib/microfx/provision-health.sh" \
  MICROFX_PROVISION_SERVICE="$scenario/bin/provision-service" \
  MICROFX_PROVISION_STATUS="$scenario/run/microfx-provision-status" \
  MICROFX_PROVISION_WATCHDOG_LOG="$scenario/watchdog.log" \
  MICROFX_PROVISION_WATCHDOG_INITIAL_DELAY=0 \
  MICROFX_PROVISION_WATCHDOG_POLL_DELAY=0 \
  MICROFX_PROVISION_WATCHDOG_REPAIR_DELAY=0 \
  MICROFX_PROVISION_WATCHDOG_MAX_CYCLES=3 \
    "$OVERLAY/usr/sbin/provision-watchdog"
}

run_provision provision-failure 0 0
microfx_assert_file_count 1 '^repair repair$' "$TEMP/provision-failure/actions" "setup service repair threshold"
run_provision provision-stale-processes 1 0
microfx_assert_file_count 1 '^repair repair$' "$TEMP/provision-stale-processes/actions" "unusable setup service repair threshold"
run_provision provision-disabled-beacon 1 1 DISABLED
microfx_assert_file_count 1 '^repair repair$' "$TEMP/provision-disabled-beacon/actions" "disabled setup beacon repair threshold"
run_provision provision-healthy 1 1
microfx_assert_file_empty "$TEMP/provision-healthy/actions" "healthy setup services triggered repair"
microfx_assert_file_contains '^state[[:space:]]healthy$' "$TEMP/provision-healthy/run/microfx-provision-status" "healthy setup status"
microfx_assert_file_contains '^portal[[:space:]]1$' "$TEMP/provision-healthy/run/microfx-provision-status" "setup HTTP status"
microfx_assert_file_contains '^beacon[[:space:]]1$' "$TEMP/provision-healthy/run/microfx-provision-status" "setup beacon status"

microfx_test_finish "Wi-Fi and setup-service watchdog runtime tests"
