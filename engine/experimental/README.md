# Layer compositor prototype

This directory defines a portable, pure planning contract. It is deliberately
not linked into the firmware renderer yet. The current one-scene GLES path
remains authoritative for development and deployment.

The planner makes the eventual split explicit:

- scenes, effects, and non-normal blend modes stay in a GLES render target;
- a native-density UI layer may use an overlay plane only when the platform
  proves DMA-BUF import, z-position, scaling, and any requested global alpha;
- every unsupported case deterministically falls back to GLES composition.

Layer order is back-to-front. Because the portable design has one flattened
GLES target, native planes may represent only a contiguous topmost suffix of
that order. The planner chooses planes from front to back: if a top effect,
unsupported blend, or exhausted plane budget forces one layer into GLES, every
lower layer also stays in the GLES target. This prevents an optimization from
silently moving UI in front of a later effect. `firstNativeLayer`,
`nativePlaneCount`, and `glesLayerCount` make that boundary inspectable.

JavaScript will describe visual intent, not an i.MX6-specific plane.
`layers.mjs` now maps that future retained facade: it owns ordered layer
membership, logical dimensions, orientation, blend, opacity, effects, and
per-layer pixel density, then emits the exact numeric requests consumed by the
C planner. `layer_stack.c` validates the same bounded descriptor before
planning. Both implementations are test-only prototypes; neither is embedded
in the runtime or linked into the appliance renderer. The single GLES renderer
remains authoritative.

The contract also adopts Aura's useful rule that render-target orientation and
logical dimensions must be explicit. A later target implementation must carry
those fields and perform exactly one presentation flip; this prototype
allocates no buffers and therefore cannot disturb the current page-flip path.
