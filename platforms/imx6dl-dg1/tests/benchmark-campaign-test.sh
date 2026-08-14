#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLATFORM_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
ROOT=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
. "$ROOT/tests/lib/microfx-test.sh"
WORK=$(microfx_test_tempdir)
trap 'rm -rf "$WORK"' EXIT

run_root=$WORK/run
data_root=$WORK/data
log=$WORK/canvas.log
mkdir -p "$run_root" "$data_root/apps/projects/demo"
ln -s "$data_root/apps/projects/demo" "$data_root/apps/current"

override=$PLATFORM_DIR/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-benchmark-override
capture=$PLATFORM_DIR/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-benchmark-capture

MICROFX_RUN_ROOT=$run_root "$override" native-fixed
microfx_assert_file_contains '^MICROFX_OUTPUT_WIDTH=0$' "$run_root/microfx-benchmark.env" "native width is automatic"
microfx_assert_file_contains '^MICROFX_PIXEL_DENSITY=1.00$' "$run_root/microfx-benchmark.env" "native density is fixed"
microfx_assert_file_contains '^MICROFX_PROFILE_INTERVAL=60$' "$run_root/microfx-benchmark.env" "campaign reporting is bounded"
MICROFX_RUN_ROOT=$run_root "$override" 720-fixed-60
microfx_assert_file_contains '^MICROFX_OUTPUT_WIDTH=1280$' "$run_root/microfx-benchmark.env" "720p 60 profile fixes output width"
microfx_assert_file_contains '^MICROFX_TARGET_FPS=60$' "$run_root/microfx-benchmark.env" "720p 60 profile selects 60 fps"
MICROFX_RUN_ROOT=$run_root "$override" 1080-quality
microfx_assert_file_contains '^MICROFX_COLOR_FORMAT=rgba8888$' "$run_root/microfx-benchmark.env" "quality profile selects high color"
microfx_assert_file_contains '^MICROFX_ANTIALIASING=msaa4$' "$run_root/microfx-benchmark.env" "quality profile selects MSAA"
MICROFX_RUN_ROOT=$run_root "$override" clear
microfx_assert_eq no "$([ -e "$run_root/microfx-benchmark.env" ] && echo yes || echo no)" "clear removes volatile overrides"

cat >"$WORK/reload" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$MICROFX_TEST_CALLS"
printf '%s\t%s\trunning\tfixture\n' "$2" "$1" >"$MICROFX_RUN_ROOT/microfx-project-status"
cat >>"$MICROFX_RENDER_LOG" <<LOG
MICROFX_PROFILE frames=60 output=1920x1080 density=0.750 fps=30 target_fps=30 budget=33.333 script=1 begin=1 background=2 mesh=4 overlay=2 interface=1 present=20 cpu=10 noncpu=21 wall=31 max_wall=34 over_budget=2
DRM_TIMING swaps=60 egl=0.7 lock=0.4 addfb=0.1 wait_previous=10 flip_submit=0.2 post_submit=0.3 total=11.7
LOG
EOF
cat >"$WORK/sleep" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$WORK/reload" "$WORK/sleep"
: >"$WORK/calls"

MICROFX_RUN_ROOT=$run_root MICROFX_DATA_ROOT=$data_root \
MICROFX_RENDER_LOG=$log MICROFX_BENCHMARK_OVERRIDE=$override \
MICROFX_RELOAD_TOOL=$WORK/reload MICROFX_SLEEP_TOOL=$WORK/sleep \
MICROFX_TEST_CALLS=$WORK/calls \
  "$capture" native-75 2 >"$WORK/capture.log"
microfx_assert_file_contains 'MICROFX_PROFILE frames=60' "$WORK/capture.log" "capture returns renderer records"
microfx_assert_file_contains 'DRM_TIMING swaps=60' "$WORK/capture.log" "capture returns DRM records"
microfx_assert_file_count 2 'demo benchmark-' "$WORK/calls" "capture starts and restores the renderer"
microfx_assert_eq no "$([ -e "$run_root/microfx-benchmark.env" ] && echo yes || echo no)" "capture clears volatile overrides"
microfx_assert_eq no "$([ -e "$run_root/microfx-profile" ] && echo yes || echo no)" "capture restores disabled profiling"

touch "$run_root/microfx-profile"
MICROFX_RUN_ROOT=$run_root MICROFX_DATA_ROOT=$data_root \
MICROFX_RENDER_LOG=$log MICROFX_BENCHMARK_OVERRIDE=$override \
MICROFX_RELOAD_TOOL=$WORK/reload MICROFX_SLEEP_TOOL=$WORK/sleep \
MICROFX_TEST_CALLS=$WORK/calls \
  "$capture" native-fixed 1 >/dev/null
microfx_assert_eq yes "$([ -e "$run_root/microfx-profile" ] && echo yes || echo no)" "capture preserves pre-existing profiling"
rm -f "$run_root/microfx-profile"

cat >"$WORK/ssh" <<'EOF'
#!/bin/sh
set -eu
[ "$1" = fixture.local ]
[ "$2" = /usr/sbin/microfx-benchmark-capture ]
[ "$4" = 1 ]
printf '%s\n' "$3" >>"$MICROFX_TEST_CALLS"
case "$3" in
  native-fixed) output=1920x1080 density=1.000 wall=32 ;;
  720-fixed) output=1280x720 density=1.000 wall=20 ;;
  *) exit 2 ;;
esac
printf 'MICROFX_PROFILE frames=60 output=%s density=%s fps=30 target_fps=30 budget=33.333 script=1 begin=1 background=2 mesh=4 overlay=2 interface=1 present=10 cpu=9 noncpu=11 wall=%s max_wall=34 over_budget=1\n' "$output" "$density" "$wall"
printf 'DRM_TIMING swaps=60 egl=0.7 lock=0.4 addfb=0.1 wait_previous=8 flip_submit=0.2 post_submit=0.3 total=9.7\n'
EOF
chmod +x "$WORK/ssh"
: >"$WORK/campaign-calls"
if ! MICROFX_SSH_WRAPPER=$WORK/ssh MICROFX_TEST_CALLS=$WORK/campaign-calls \
  MICROFX_BENCHMARK_PROFILES='native-fixed 720-fixed' MICROFX_BENCHMARK_SECONDS=1 \
    "$PLATFORM_DIR/scripts/canvas-benchmark.sh" fixture.local "$WORK/evidence" \
    >"$WORK/matrix.stdout" 2>"$WORK/campaign.stderr"; then
  cat "$WORK/campaign.stderr" >&2
  microfx_test_fail "benchmark campaign fixture failed"
fi
microfx_assert_file_count 1 '^native-fixed$' "$WORK/campaign-calls" "native campaign runs once"
microfx_assert_file_count 1 '^720-fixed$' "$WORK/campaign-calls" "720p campaign runs once"
microfx_assert_file_contains '1920x1080' "$WORK/evidence/matrix.txt" "matrix contains native run"
microfx_assert_file_contains '1280x720' "$WORK/evidence/matrix.txt" "matrix contains 720p run"
microfx_assert_file_contains '"drmSwaps": 60' "$WORK/evidence/native-fixed.json" "individual evidence retains DRM attribution"
microfx_assert_file_contains 'Saved benchmark evidence' "$WORK/campaign.stderr" "campaign reports evidence location"

microfx_test_finish "volatile benchmark campaign"
