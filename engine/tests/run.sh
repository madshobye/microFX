#!/bin/sh
set -eu

ENGINE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-engine-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/include" \
  "$ENGINE_DIR/src/scene.c" "$ENGINE_DIR/tests/scene_test.c" \
  -o "$WORK/scene-test"
"$WORK/scene-test"

mkdir -p "$WORK/project" "$WORK/outside"
touch "$WORK/project/main.js" "$WORK/project/model.obj" "$WORK/outside/outside.obj"
ln -s "$WORK/outside/outside.obj" "$WORK/project/escape.obj"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/include" \
  "$ENGINE_DIR/src/assets.c" "$ENGINE_DIR/tests/assets_test.c" \
  -o "$WORK/assets-test"
"$WORK/assets-test" "$WORK/project/main.js" "$WORK/project/model.obj" \
  "$WORK/outside/outside.obj" "asset containment tests passed"
echo "retained scene tests passed"
