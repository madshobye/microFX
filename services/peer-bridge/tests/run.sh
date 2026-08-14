#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERVICE_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
REPO=$(CDPATH= cd -- "$SERVICE_DIR/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-peer-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# cJSON is already a pinned firmware dependency. Reuse the checked-out copy
# available beside p1_embed so the host suite exercises the real C++ protocol
# without asking developers to install another Homebrew package.
CJSON=${MICROFX_CJSON_DIR:-$REPO/../Portal/externallibs_modified/sepfy__libpeer/third_party/cJSON}
if [ ! -r "$CJSON/cJSON.c" ] || [ ! -r "$CJSON/cJSON.h" ]; then
  echo "Missing cJSON source; set MICROFX_CJSON_DIR to a cJSON source directory" >&2
  exit 1
fi
mkdir -p "$WORK/cjson"
cp "$CJSON/cJSON.h" "$WORK/cjson/cJSON.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$WORK" -c "$CJSON/cJSON.c" -o "$WORK/cJSON.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I"$WORK" -I"$SERVICE_DIR/src" \
  "$SERVICE_DIR/src/project_protocol.cpp" "$TEST_DIR/project_protocol_test.cpp" \
  "$WORK/cJSON.o" -o "$WORK/project-protocol-test"
"$WORK/project-protocol-test"

${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I"$WORK" -I"$SERVICE_DIR/src" \
  "$SERVICE_DIR/src/project_protocol.cpp" "$TEST_DIR/protocol_cli.cpp" \
  "$WORK/cJSON.o" -o "$WORK/protocol-cli"
MICROFX_PROTOCOL_CLI="$WORK/protocol-cli" \
MICROFX_SUPERVISOR="$REPO/platforms/imx6dl-dg1/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/canvas-supervisor" \
  node --test "$REPO/web/editor/tests/device-integration.test.mjs"
