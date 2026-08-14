#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 "$ROOT/tests/protocol-contract-test.py"
"$ROOT/engine/tests/run.sh"
"$ROOT/apps/tests/run.sh"
"$ROOT/services/provision/tests/run.sh"
"$ROOT/services/peer-bridge/tests/run.sh"
"$ROOT/platforms/imx6dl-dg1/tests/wifi-policy-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/watchdog-runtime-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/recovery-client-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/time-sync-runtime-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/radio-policy-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/device-identity-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/provision-policy-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/provision-content-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/provision-service-runtime-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/firmware-layout-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/platform-hardening-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/prototype-isolation-test.sh"
python3 "$ROOT/platforms/imx6dl-dg1/tests/compositor-probe-test.py"
"$ROOT/platforms/imx6dl-dg1/tests/reload-tool-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/network-status-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/wifi-connect-runtime-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/hardware-smoke-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/benchmark-campaign-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/data-adapter-policy-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/build-cache-policy-test.sh"
"$ROOT/platforms/imx6dl-dg1/tests/supervisor-test.sh"
npm test --prefix "$ROOT/web/editor"

echo "microFX host test suite passed"
