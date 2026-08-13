# microFX Engine

The microFX engine embeds QuickJS and exposes retained GPU resources rather than an
immediate-mode drawing API. It currently provides four compact batches:

- cheap retained quads/triangle-fan circles;
- explicit circles and rounded rectangles as signed-distance fields;
- cubes and low-poly spheres with GPU-side transforms and material effects;
- text backed by raylib's existing font atlas and a retained glyph VBO.

Each batch is one GLES2 draw call. Geometry and glyph buffers are rebuilt only
when construction or text changes; normal animation updates compact object
state.

```js
fx.configure({
  outputWidth: 0,
  outputHeight: 0,
  pixelDensity: "auto",
  minimumPixelDensity: 0.5,
  targetFps: 30,
  debugBar: true,
  durationSeconds: 0
});

const background = fx.gradientRect(960, 540, 1920, 1080,
                                   0x142040ff, 0x080c1dff);
const dot = fx.circle(200, 200, 40, 0xffcc33ff);
const panel = fx.sdfRoundedRect(600, 400, 300, 120, 24, 0x202850dd);
const cube = fx.cube(0, 1, 0, 1.2, 0xffcc33ff);
const model = fx.model("models/icosahedron.obj", 0, 1, 0, 2.4, 0x65d9ffff);
const label = fx.text("hello GPU", 80, 160, 32, 0xffffffff);

fx.camera(0, 3, 9, 0, 1, 0, 48);
fx.effect(cube, fx.effects.gradient, 0.9, 1.0);

function update(time, delta) {
  fx.move(dot, 200 + Math.sin(time) * 100, 200, 0);
  fx.transform(cube, 0, 1, 0, time * 0.2, time * 0.5, 0, 1.2);
}
```

2D and text coordinates use the 1920×1080 design space and map to the selected
global HDMI mode. 3D uses world coordinates and radians. Colors are
`0xRRGGBBAA`. Construction belongs at script load; `update` should mutate
handles.

## JavaScript API

| Call | Retained operation |
| --- | --- |
| `rect(x,y,w,h,color)` | Add a cheap solid GPU quad |
| `gradientRect(x,y,w,h,top,bottom)` | Add a cheap vertical-gradient quad |
| `background(top,bottom)` | Add a gradient rendered before the 3D scene |
| `circle(x,y,r,color)` | Add a fast retained triangle-fan circle |
| `sdfCircle(x,y,r,color)` | Add an experimental SDF circle |
| `sdfRoundedRect(x,y,w,h,r,color)` | Add an SDF rounded rectangle |
| `move(handle,x,y,rotation)` | Mutate a 2D element |
| `cube(x,y,z,size,color)` | Add a unit-cube primitive |
| `sphere(x,y,z,size,color)` | Add a low-poly sphere |
| `wireCube(x,y,z,size,color)` | Add a retained 3D cube outline |
| `grid(x,y,z,size,color)` | Add a retained horizontal 3D grid |
| `model(path,x,y,z,size,color)` | Load a project-relative model asset |
| `transform(handle,x,y,z,rx,ry,rz,size)` | Mutate a 3D primitive |
| `camera(x,y,z,tx,ty,tz,fovY)` | Set retained perspective camera |
| `text(value,x,y,size,color)` | Add retained atlas text |
| `setText(handle,value)` | Rebuild text only when content changes |
| `color(handle,color)` | Change any retained element's color |
| `effect(handle,kind,amount,scale)` | Select a GPU material effect |
| `configure(settings)` | Set startup output/FPS policy from the application |
| `debugBar(visible)` | Show or hide the native runtime diagnostics bar |
| `env(name,fallback)` | Read deployment information supplied to the app |

`fx.product` supplies the centralized product identity (`name`, `slug`,
`defaultPeerId`, `defaultSetupSsid`, and `defaultSetupPassword`). Its compiled
defaults live in `engine/include/microfx/identity.h`; applications should use
these values instead of repeating product-name literals.

Material constants are `fx.effects.solid`, `gradient`, `noise`, and `bands`.
Effects execute per fragment; JavaScript updates only their retained parameters.
`fx.math.hash2(x,y)`, `noise2(x,y)`, and `lerp(a,b,t)` are deterministic helpers
for object animation. Per-pixel procedural math stays in the material shader;
shared GLES2 length/square-root, hash and smooth-noise fragments live in
`engine/include/microfx/gpu_math.h` and are compiled directly into GPU programs.

Use quads for large solid or gradient surfaces. SDF remains available for
scalable curves, rounded corners, and experiments, but its per-fragment distance
math has a measurable fill cost on the GC880. `circle()` uses retained geometry;
experimental distance-field shapes must be requested explicitly with the
`sdfCircle()` or `sdfRoundedRect()` names.

`configure()` runs while the script is evaluated, before DRM/GL initialization.
Both output dimensions must be zero (native/automatic) or both non-zero (fixed).
Environment variables are the final deployment override. `debugBar()` may also
be called from `update()` and takes effect on the next frame.

The JavaScript application always owns the complete frame; the engine contains
no built-in application scene. A positive `durationSeconds` exits the
application cleanly after that interval; zero runs indefinitely. The firmware
uses this control for its portable 40-second onboarding application.

Model assets belong beside the application script or in a directory below it.
`model()` accepts only a relative path. The runtime resolves both the project
directory and asset to canonical paths, then rejects absolute paths, missing or
non-regular files, `..` traversal outside the project, and symlinks that escape
the project directory. This is a basic project boundary for application assets,
not a security sandbox for untrusted JavaScript.

Current limits are 256 SDF elements, 16 mesh elements, 32 text elements of 127
bytes each, a 16 MiB JS heap, and a 512 KiB JS stack. A script exception is fatal
by design so a bad remote revision remains visible through independent SSH
rather than silently changing behavior.

`apps/demo/scripts/text-benchmark.js` isolates the retained glyph path at fixed
native density. On the target GC880, three static strings retain the same
1920x1080/30 FPS tier as the native demo without JS overlays. Static text builds
one glyph VBO once and submits it in one draw; changing text rebuilds that VBO.
Large SDF-covered areas are a separate fragment-fill cost and should not be used
to infer text performance.

Future renderer bindings must preserve this model: bounded object arrays,
static/dirty GPU buffers, atlased text, and one draw per material/batch—not one
draw per JavaScript call.
