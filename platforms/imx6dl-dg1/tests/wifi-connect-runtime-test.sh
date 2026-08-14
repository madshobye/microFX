#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
. "$REPO/tests/lib/microfx-test.sh"
CONNECT=$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/wifi-connect
TEMP=$(microfx_test_tempdir)
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM

mkdir -p "$TEMP/bin" "$TEMP/run/wpa_supplicant" "$TEMP/sys/wlan1"
printf '%s\n' 'network={' '  ssid="stored"' '}' >"$TEMP/wpa.conf"
: >"$TEMP/actions"
: >"$TEMP/run/wpa_supplicant/wlan1"

cat >"$TEMP/network-status.sh" <<'EOF'
microfx_write_network_status() {
  printf 'status %s %s\n' "$1" "$3" >>"$MOCK_ACTIONS"
}
EOF
cat >"$TEMP/bin/command-mock" <<'EOF'
#!/bin/sh
name=${0##*/}
case "$name" in
  date) echo test-time ;;
  sleep) : ;;
  iw) : ;;
  ip) : ;;
  wpa_cli) echo wpa_state=DISCONNECTED ;;
  wpa_supplicant)
    [ ! -e "$MOCK_CONTROL_SOCKET" ] && echo stale-socket-cleared >>"$MOCK_ACTIONS"
    pidfile=
    while [ "$#" -gt 0 ]; do
      [ "$1" != -P ] || { shift; pidfile=$1; }
      shift
    done
    printf '%s\n' "$$" >"$pidfile"
    : >"$MOCK_CONTROL_SOCKET"
    ;;
  udhcpc) echo unexpected-dhcp >>"$MOCK_ACTIONS" ;;
esac
EOF
chmod +x "$TEMP/bin/command-mock"
for name in date sleep iw ip wpa_cli wpa_supplicant udhcpc; do
  ln -s command-mock "$TEMP/bin/$name"
done

if PATH="$TEMP/bin:$PATH" \
  MOCK_ACTIONS="$TEMP/actions" \
  MOCK_CONTROL_SOCKET="$TEMP/run/wpa_supplicant/wlan1" \
  MICROFX_RUNTIME_CONFIG="$TEMP/missing-runtime.conf" \
  MICROFX_PRODUCT_CONFIG="$TEMP/missing-product.conf" \
  MICROFX_NETWORK_STATUS_LIBRARY="$TEMP/network-status.sh" \
  MICROFX_WIFI_CONFIG="$TEMP/wpa.conf" \
  MICROFX_RUN_ROOT="$TEMP/run" \
  MICROFX_SYS_CLASS_NET="$TEMP/sys" \
  MICROFX_WIFI_CONNECT_MAX_ROUNDS=2 \
  MICROFX_WIFI_CONNECT_ASSOCIATION_SECONDS=1 \
  MICROFX_WIFI_CONNECT_RETRY_DELAY=0 \
    "$CONNECT"; then
  microfx_test_fail "failed association returned success"
fi

microfx_assert_file_contains '^stale-socket-cleared$' "$TEMP/actions" "stale supplicant socket reached a new process"
microfx_assert_file_contains '^status disconnected wlan1$' "$TEMP/actions" "failed association status missing"
test ! -e "$TEMP/run/wpa_supplicant/wlan1" || microfx_test_fail "failed association retained its control socket"
test ! -e "$TEMP/run/wpa_supplicant-wlan1.pid" || microfx_test_fail "failed association retained its pidfile"

microfx_test_finish "normal Wi-Fi connector cleanup"
