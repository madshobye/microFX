# i.MX6DL composition mapping

This is an exploratory hardware map, not an enabled render path. The normal
microFX firmware continues to use one OpenGL ES 2 scene through Etnaviv and
DRM/KMS. Nothing in this document changes the build, boot, SD-image, or upload
workflow.

## Available blocks

- **Vivante GC880** renders the existing OpenGL ES scene.
- **i.MX IPU display planes** may be able to scale a lower-density scene buffer
  to the native HDMI mode and combine it with native-resolution 2D planes. The
  exact plane properties depend on what the kernel's i.MX DRM driver exposes.
- **Vivante GC320 / g2d** is a separate 2D engine, but the commonly available
  userspace API belongs to vendor graphics stacks. It should not become a hard
  dependency unless a redistributable, maintainable interface is established.

## Candidate pipeline

1. Keep the HDMI connector at its preferred native mode.
2. Render the 3D/effects scene with GC880 into a density-controlled buffer.
3. Import that buffer without a CPU copy and let an IPU-backed DRM plane scale
   it to the output rectangle.
4. Put text, diagnostics, and other crisp 2D layers in native-resolution DRM
   planes when the hardware exposes enough planes and alpha/z-order controls.
5. Fall back to the existing single GLES composition path when a requested
   blend, format, or plane assignment is unsupported.

The portable planner additionally requires every native-plane assignment to be
a topmost contiguous suffix of the visual stack. A later GLES effect cannot be
placed above a lower native plane when all GLES layers share one render target;
that case deliberately keeps both layers in GLES. When planes are scarce, the
highest eligible UI layers get them and lower UI is flattened with the scene.

This would make `pixelDensity` independent of the HDMI mode. It would also
avoid asking the 3D renderer to redraw native-resolution UI solely to combine
layers. It is not yet known whether this board/kernel combination exposes the
required scaling, alpha, z-position, and buffer-import properties.

## Hardware probe required

Before implementing the pipeline, capture:

- DRM connectors, CRTCs, encoders, and planes;
- supported plane pixel formats;
- source and destination size limits and scaling ratios;
- alpha, blend-mode, rotation, and z-position properties;
- atomic modesetting support;
- DMA-BUF export/import support between Etnaviv and i.MX DRM;
- page-flip and plane-update timing without CPU buffer copies.

`scripts/drm-plane-probe.sh` gathers the read-only inventory when `modetest` is
available on the target. Save that output and pass it to
`scripts/evaluate-drm-plane-probe.py`. The evaluator records plane types,
formats, alpha and z-position controls, but deliberately keeps the GLES
baseline until active experiments have also proved scaling, DMA-BUF import,
atomic commits, and page-flip synchronization. Evidence is represented as
explicit `MICROFX_EVIDENCE name=yes` lines so absence is never mistaken for
support.

A later hardware prototype should first display one scaled test buffer plus
one native overlay and append those active results to its capture. It must
remain optional until it has passed visual, performance, and reboot tests; the
current GLES path stays the general-development baseline.
