#!/bin/sh
set -eu

APPS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
count=0

if find "$APPS_DIR" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
    | grep -q .; then
  echo "Portable applications must contain only scripts, metadata, and assets" >&2
  exit 1
fi

node --check "$APPS_DIR/demo/scripts/main.js" >/dev/null
python3 -m json.tool "$APPS_DIR/demo/project.json" >/dev/null
python3 "$APPS_DIR/tools/generate-image-assets.py" --check >/dev/null

for project in "$APPS_DIR"/projects/*; do
  [ -d "$project" ] || continue
  test -s "$project/main.js"
  test -s "$project/project.json"
  node --check "$project/main.js" >/dev/null
  python3 -m json.tool "$project/project.json" >/dev/null
  if grep -Eq 'fx\._[[:alnum:]_]+' "$project/main.js"; then
    echo "Project uses private native binding: $project" >&2
    exit 1
  fi
  count=$((count + 1))
done

node "$APPS_DIR/tests/validate-assets.mjs"
node --test "$APPS_DIR/tests/runtime-test.test.mjs"
node --test "$APPS_DIR/tests/network-runtime.test.mjs"
node "$APPS_DIR/tests/runtime-harness.mjs"

if [ "$count" -ne 13 ]; then
  echo "Expected exactly 13 selectable bundled projects plus the fallback demo, found $count" >&2
  exit 1
fi

echo "bundled project tests passed ($count projects)"
