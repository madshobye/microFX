#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-provision-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -Werror "$TEST_DIR/save_test.c" \
  -o "$WORK/save-test"
"$WORK/save-test"

${CC:-cc} -std=c11 -Wall -Wextra -Werror "$TEST_DIR/control_test.c" \
  -o "$WORK/control-test"
"$WORK/control-test"

node --test "$TEST_DIR/portal-ui.test.mjs"

"$TEST_DIR/portal-supervisor-test.sh"
