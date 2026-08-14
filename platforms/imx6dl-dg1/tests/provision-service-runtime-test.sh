#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
. "$REPO/tests/lib/microfx-test.sh"
OVERLAY=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay
FIXTURES=$ROOT/tests/fixtures
TEMP=$(microfx_test_tempdir)
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

scenario=$TEMP/setup
mkdir -p "$scenario/bin" "$scenario/run" "$scenario/sys/ap0" "$scenario/sys/sta0"
printf '%s\n' '00:03:7f:be:f0:a0' >"$scenario/sys/sta0/address"
: >"$scenario/actions"
: >"$scenario/hostapd-attempts"

for name in hostapd hostapd_cli dnsmasq httpd ip iw sleep; do
  ln -s "$FIXTURES/provision-service-command-mock" "$scenario/bin/$name"
done

run_service() {
  PATH="$scenario/bin:$PATH" \
  MOCK_ACTIONS="$scenario/actions" \
  MOCK_HOSTAPD_ATTEMPTS="$scenario/hostapd-attempts" \
  MICROFX_RUN_ROOT="$scenario/run" \
  MICROFX_SYS_CLASS_NET="$scenario/sys" \
  MICROFX_PROVISION_INTERFACE=${TEST_PROVISION_INTERFACE:-ap0} \
  MICROFX_WIFI_INTERFACE=${TEST_WIFI_INTERFACE:-sta0} \
  MICROFX_PRODUCT_CONFIG="$OVERLAY/etc/microfx-product.conf" \
  MICROFX_HOSTAPD_TEMPLATE="$OVERLAY/etc/hostapd-microfx.conf" \
  MICROFX_DNSMASQ_CONFIG="$OVERLAY/etc/dnsmasq-microfx.conf" \
  MICROFX_WWW_SOURCE="$REPO/services/provision/www" \
  MICROFX_STUDIO_SOURCE="$REPO/web/editor" \
  MICROFX_PROVISION_HEALTH_LIBRARY="$OVERLAY/usr/lib/microfx/provision-health.sh" \
  MICROFX_PROVISION_CONTENT_LIBRARY="$OVERLAY/usr/lib/microfx/provision-content.sh" \
  MICROFX_RADIO_POLICY_LIBRARY="$OVERLAY/usr/lib/microfx/radio-policy.sh" \
  MICROFX_PROVISIONING=${TEST_PROVISIONING:-1} \
    "$OVERLAY/etc/init.d/S40provision" "$1"
}

TEST_PROVISIONING=0 run_service start
disabled_attempts=$(cat "$scenario/hostapd-attempts")
microfx_assert_eq 0 "${disabled_attempts:-0}" "disabled setup network attempted hostapd"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
if grep -Eq '^(hostapd|dnsmasq|httpd) ' "$scenario/actions"; then
  microfx_test_fail "disabled setup network launched a service"
fi
unset TEST_PROVISIONING

run_service repair

microfx_assert_eq 2 "$(cat "$scenario/hostapd-attempts")" "hostapd retry count"
microfx_assert_file_contains '^ssid=microFX-setup$' "$scenario/run/hostapd-microfx.conf" "rendered setup SSID"
microfx_assert_file_contains '^wpa_passphrase=microfxsetup$' "$scenario/run/hostapd-microfx.conf" "rendered setup password"
microfx_assert_file_contains '^interface=ap0$' "$scenario/run/hostapd-microfx.conf" "rendered hostapd interface"
microfx_assert_file_contains '^interface=ap0$' "$scenario/run/dnsmasq-microfx.conf" "rendered dnsmasq interface"
microfx_assert_file_contains '^ip link set ap0 address 02:03:7f:be:f0:a1$' "$scenario/actions" "distinct AP address"
microfx_assert_file_contains '^iw dev ap0 set type __ap$' "$scenario/actions" "AP interface mode"
microfx_assert_file_contains '^ip address add 10.42.0.1/24 dev ap0$' "$scenario/actions" "setup address"
microfx_assert_file_contains "^dnsmasq .*${scenario}/run/dnsmasq-microfx.conf" "$scenario/actions" "DHCP and wildcard DNS startup"
microfx_assert_file_contains '^httpd -f -p 80 ' "$scenario/actions" "setup portal startup"

for service in hostapd dnsmasq httpd; do
  pidfile=$scenario/run/${service}-microfx.pid
  MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
  [ -r "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null ||
    microfx_test_fail "$service did not remain alive"
done

MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
[ -L "$scenario/run/microfx-www/hotspot-detect.html" ] ||
  microfx_test_fail "Apple captive probe was not installed"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
[ -L "$scenario/run/microfx-www/portal-app.js" ] ||
  microfx_test_fail "portal browser controller was not published"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
[ "$(readlink "$scenario/run/microfx-www/portal-app.js")" = "$REPO/services/provision/www/portal-app.js" ] ||
  microfx_test_fail "portal browser controller points at the wrong source"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
[ -L "$scenario/run/microfx-www/studio" ] ||
  microfx_test_fail "Studio was not published by the management service"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
[ "$(readlink "$scenario/run/microfx-www/studio")" = "$REPO/web/editor" ] ||
  microfx_test_fail "Studio points at the wrong source"
MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
if grep -Eq '(^| )sta0( |$)' "$scenario/actions"; then
  microfx_test_fail "setup service disturbed the independent client interface"
fi

run_service stop
for service in hostapd dnsmasq httpd; do
  MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
  [ ! -e "$scenario/run/${service}-microfx.pid" ] ||
    microfx_test_fail "$service pidfile survived stop"
done
microfx_assert_file_contains '^ip link set ap0 down$' "$scenario/actions" "AP interface cleanup"

MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
if TEST_PROVISION_INTERFACE=shared0 TEST_WIFI_INTERFACE=shared0 \
    run_service repair >/dev/null 2>&1; then
  microfx_test_fail "setup service accepted one interface for both radio roles"
fi

microfx_test_finish "setup AP service runtime"
