#!/bin/sh
set -eu

cat >&2 <<'EOF'
build.sh is intentionally disabled because it was ambiguous.

Choose one explicit workflow:
  ./scripts/build-graphics.sh        engine, renderer, shaders and JS (normal)
  ./scripts/build-linux.sh           complete Linux/root-filesystem image
  ./scripts/build-linux.sh --clean   archive the full cache, then rebuild everything
EOF
exit 2
