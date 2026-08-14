#!/bin/sh
set -eu

ENGINE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-engine-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/include" \
  "$ENGINE_DIR/src/scene.c" "$ENGINE_DIR/tests/scene_test.c" \
  -o "$WORK/scene-test"
"$WORK/scene-test"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/include" \
  "$ENGINE_DIR/src/quality.c" "$ENGINE_DIR/tests/quality_test.c" \
  -o "$WORK/quality-test"
"$WORK/quality-test"
python3 "$ENGINE_DIR/tests/profile_report_test.py"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/experimental" \
  "$ENGINE_DIR/experimental/compositor_plan.c" \
  "$ENGINE_DIR/experimental/compositor_plan_test.c" \
  -o "$WORK/compositor-plan-test"
"$WORK/compositor-plan-test"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/experimental" \
  "$ENGINE_DIR/experimental/compositor_plan.c" \
  "$ENGINE_DIR/experimental/layer_stack.c" \
  "$ENGINE_DIR/experimental/layer_stack_test.c" \
  -o "$WORK/layer-stack-test"
"$WORK/layer-stack-test"
node --test "$ENGINE_DIR/experimental/layers.test.mjs"

mkdir -p "$WORK/project/assets" "$WORK/outside" "$WORK/volatile/project"
touch "$WORK/project/main.js" "$WORK/project/assets/model.obj" "$WORK/outside/outside.obj"
printf '%s\n' '{"value":"volatile"}' >"$WORK/volatile/project/feed.json"
ln -s "$WORK/outside/outside.obj" "$WORK/project/assets/escape.obj"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$ENGINE_DIR/include" \
  "$ENGINE_DIR/src/assets.c" "$ENGINE_DIR/tests/assets_test.c" \
  -o "$WORK/assets-test"
"$WORK/assets-test" "$WORK/project/main.js" "$WORK/project/assets/model.obj" \
  "$WORK/outside/outside.obj" "$WORK/volatile" \
  "$WORK/volatile/project/feed.json" "asset containment tests passed"

python3 "$ENGINE_DIR/tools/embed-runtime.py" \
  "$ENGINE_DIR/runtime/retained.js" "$WORK/runtime_js.inc"
cmp "$WORK/runtime_js.inc" "$ENGINE_DIR/src/runtime_js.inc"
node --test "$ENGINE_DIR/runtime/tests/retained.test.mjs"
echo "retained scene tests passed"
