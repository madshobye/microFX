# Retired i.MX6DL IPU scaling research (2026-08)

This folder preserves a discontinued hardware experiment. Nothing here is part
of the normal firmware build, VM smoke test, host test suite, upload workflow,
runtime supervisor, or boot path. The supported DG1 renderer is the single GLES
path with automatic density implemented by selecting an advertised HDMI mode
and restarting the renderer.

Do not run these scripts on a deployed unit without reviewing and adapting
their old relative paths. The IPU test path proved capable of crashing the DG1,
and the archived one-test-per-boot guard was added only after that failure.

## Final decision

The route was dropped because its modest performance gain did not justify its
driver and synchronization complexity:

- direct DRM primary and overlay plane scaling were rejected by the kernel;
- `/dev/video4` could scale DMA-BUFs, but conversion cost about 12.2 ms/frame;
- the corrected production prototype at fixed density 0.5 rendered 960x540 and
  scanned out 1920x1080 at about 30 fps, versus roughly 20 fps for the tested
  full-resolution scene;
- blocking atomic presentation cost another 4.6-5.0 ms, while total serialized
  presentation remained about 28.6-29.3 ms/frame;
- reaching 60 fps would require a substantially more complex asynchronous,
  multi-buffer pipeline across EGL, V4L2 M2M, DMA-BUF, and KMS;
- one display blink occurred when the separate diagnostic process took over;
- starting a second IPU session in the same boot crashed the device, indicating
  unsafe kernel/driver teardown or reinitialization state.

The color corruption was understood and fixed: little-endian DRM
`XRGB8888` buffers are byte-ordered B,G,R,X, and this i.MX6 IPU V4L2 mapping
must therefore be declared as `V4L2_PIX_FMT_XBGR32`, not `XRGB32`. After that
change, saturated blue/red/green/yellow output was correct and the fixed-density
standalone pattern did not blink while running.

If this research is restarted on different hardware or a newer kernel, begin
with the standalone probes and color pattern. Do not restore the production
patch first. Require clean repeated initialization, nonblocking pipeline timing,
correct colors, sustained thermal testing, and fail-safe recovery before any
boot or runtime integration. On the current DG1, the direct advertised-HDMI-mode
strategy remains the practical solution. A lower-resolution GLES FBO upscale
was also previously measured as costing more than it saved on GC880.

## Archived contents

- `experimental/`: standalone KMS capability, IPU capability, and scaling C
  programs;
- `scripts/`: historical build/upload/evaluation wrappers, including the unsafe
  temporary runtime test harness;
- `tests/`: evaluator test and recorded success/failure fixtures;
- `production-prototype/`: the abandoned raylib patch and IPU backend include.

This is an exploratory hardware map, not an enabled render path. The normal
microFX firmware continues to use one OpenGL ES 2 scene through Etnaviv and
DRM/KMS. Nothing here changes the build, boot, SD-image, or upload workflow.

## Available blocks and observed limits

- **Vivante GC880** renders the existing OpenGL ES scene.
- **i.MX IPU display planes** can combine a primary and one overlay, but this
  image's mainline `imx-drm` driver rejects all plane scaling. Its
  `ipuv3-plane.c` validation explicitly uses `DRM_PLANE_NO_SCALING`.
- **i.MX IPU Image Converter** is exposed as the V4L2 M2M device `/dev/video4`
  (`ipu_ic_pp csc/scaler`). It accepts DMA-BUFs and can scale XRGB buffers.
- **Vivante GC320 / g2d** is a separate 2D engine, but the commonly available
  userspace API belongs to vendor graphics stacks. It should not become a hard
  dependency without a redistributable, maintainable interface.

## Hardware-proven experimental pipeline

1. Select the connected display's preferred progressive mode. This is not a
   fixed 1080p policy: if that mode is beyond the SoC/output path's capability,
   try the display's lower advertised modes and retain the highest mode that
   passes an atomic modeset test.
2. Render the 3D/effects scene with GC880 into a density-controlled buffer.
3. Export that XRGB GBM buffer as a DMA-BUF and submit it to `/dev/video4`.
4. Let IPU IC scale into a native-output-size XRGB GBM DMA-BUF, with no CPU
   copy of the primary image.
5. Scan that buffer out 1:1 on the primary plane and put one crisp native-size
   UI layer on the overlay plane.
6. Fall back to the existing single GLES composition path when a requested
   blend, format, plane assignment, or converter operation is unsupported.

The portable planner additionally requires every native-plane assignment to be
a topmost contiguous suffix of the visual stack. A later GLES effect cannot be
placed above a lower native plane when all GLES layers share one render target.
When planes are scarce, the highest eligible UI layer gets the overlay and
lower UI is flattened with the scene.

This makes experimental `pixelDensity` independent of the HDMI mode and avoids
redrawing native-resolution UI solely to combine layers. There is only one
overlay and neither active plane exposes global alpha, so multiple UI layers,
opacity, and non-normal blends still require GLES flattening.

## DG1 hardware result (2026-08-14)

The isolated probes were run on the development DG1 at `192.168.3.109`. The
connected monitor advertised and selected `1920x1080@60` as its preferred mode;
the policy and prototype select the connector preference rather than that
literal resolution.

- `card1` is `imx-drm`; atomic modesetting and PRIME import/export are present.
- The active CRTC has one primary and one overlay, both supporting XRGB8888,
  ARGB8888, and RGB565. Both have z-position, but no global alpha.
- Native-size primary plus native-size overlay committed repeatedly. Primary
  and overlay scaling were both rejected with `ERANGE`, including a 0.9 ratio.
- `/dev/video4` negotiated XRGB `960x540` to `1920x1080` and accepted DMA-BUF
  queues in both directions.
- The complete GC880 -> DMA-BUF -> IPU IC -> DMA-BUF -> KMS pipeline ran for
  30 seconds: 901 frames, 30.033 fps, 5.265% process CPU, 10.055 ms average
  render completion, 12.325 ms average IPU conversion (19.837 ms maximum), and
  10.886 ms average blocking atomic commit (15.293 ms maximum). A separate
  native overlay remained above the scaled primary and the primary used no CPU
  copy.

These early results passed `scripts/evaluate-ipu-experiment.py` as
`ipu-ic-prototype-viable`. That label established feasibility only; the later
runtime and recovery tests above rejected production adoption.
The extra native-size output buffers and full-frame DDR write are real costs,
and the current prototype synchronizes with `glFinish` and blocking queue/atomic
operations.

Before integration, benchmark representative scenes and density transitions,
test thermals and repeated reboots, exercise mode fallback (including a 4K
preferred monitor), replace avoidable blocking synchronization, and prove that
every failure returns cleanly to the GLES renderer.

## Reproducing the probe

`scripts/kms-capability-probe.sh` captures the exact KMS inventory without
changing the display. `scripts/ipu-ic-capability-probe.sh` verifies V4L2 format
negotiation and DMA-BUF queue support. `scripts/ipu-scaling-demo.sh` temporarily
stops Canvas, takes over KMS, runs the active prototype, removes its temporary
target files, and restores Canvas even on failure. It changes neither root slot
nor `/data`.

Concatenate their output and pass the capture to
`scripts/evaluate-ipu-experiment.py`. Evidence is represented as explicit
`MICROFX_EVIDENCE name=yes` lines so absence is never mistaken for support. The
archived wrappers were once called from the platform tree but are no longer
cross-compiled or executed by normal tests. The current GLES path remains the
general-development baseline.
