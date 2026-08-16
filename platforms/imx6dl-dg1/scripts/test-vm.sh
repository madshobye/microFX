#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# Kept as the portable smoke-test entry point, but it now uses the same explicit
# cache-preserving graphics workflow as development uploads.
exec "$PLATFORM_DIR/scripts/build-graphics.sh"
