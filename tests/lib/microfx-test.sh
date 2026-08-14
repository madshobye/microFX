#!/bin/sh

# Small POSIX test helpers shared by firmware service tests. This file avoids a
# target dependency: the same interaction tests run on macOS, Linux, and in the
# Buildroot VM using only the host shell and standard utilities.

MICROFX_TEST_ASSERTIONS=${MICROFX_TEST_ASSERTIONS:-0}

microfx_test_fail() {
  printf 'not ok - %s\n' "$*" >&2
  exit 1
}

microfx_test_note() {
  printf '# %s\n' "$*"
}

microfx_assert_eq() {
  expected=$1
  actual=$2
  label=${3:-values are equal}
  MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
  [ "$actual" = "$expected" ] ||
    microfx_test_fail "$label (expected '$expected', got '$actual')"
}

microfx_assert_file_contains() {
  pattern=$1
  path=$2
  label=${3:-"$path contains $pattern"}
  MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
  grep -q -- "$pattern" "$path" 2>/dev/null || microfx_test_fail "$label"
}

microfx_assert_file_count() {
  expected=$1
  pattern=$2
  path=$3
  label=${4:-"$path has $expected matching lines"}
  # Basic grep expressions match the conventions used by the small SysV
  # service tests (notably, an unescaped `|` is literal rather than alternation).
  actual=$(grep -c -- "$pattern" "$path" 2>/dev/null || true)
  microfx_assert_eq "$expected" "$actual" "$label"
}

microfx_assert_file_empty() {
  path=$1
  label=${2:-"$path is empty"}
  MICROFX_TEST_ASSERTIONS=$((MICROFX_TEST_ASSERTIONS + 1))
  [ ! -s "$path" ] || microfx_test_fail "$label"
}

microfx_test_tempdir() {
  mktemp -d "${TMPDIR:-/tmp}/microfx-test.XXXXXX"
}

microfx_test_finish() {
  label=${1:-test}
  printf 'ok - %s (%s assertions)\n' "$label" "$MICROFX_TEST_ASSERTIONS"
}
