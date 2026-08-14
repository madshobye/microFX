#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
adapter="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-data-adapters"
init="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/etc/init.d/S44data-adapters"
config="$ROOT/buildroot/configs/imx6dl_dg1_defconfig"

sh -n "$adapter" "$init"
grep -q '^BR2_PACKAGE_LIBCURL_CURL=y$' "$config"
grep -q '^BR2_PACKAGE_LIBCURL_MBEDTLS=y$' "$config"
! grep -q '^BR2_PACKAGE_LIBCURL_OPENSSL=y$' "$config"
grep -q '/run/microfx-data' "$adapter"
grep -q 'active_project' "$adapter"
grep -q -- '--max-filesize "$max_bytes"' "$adapter"
grep -q 'last-success' "$adapter"
grep -q 'waiting-clock' "$adapter"
grep -q 'MICROFX_DATA_ONCE' "$adapter"
grep -q '"$qjs_command" --std -m' "$adapter"
! grep -q '/data/apps/projects/.*/assets' "$adapter"

node --test "$ROOT/tests/data-adapters.test.mjs"
"$ROOT/tests/data-adapter-runtime-test.sh"
echo "volatile data adapter tests passed"
