# microFX Engine

The microFX engine embeds QuickJS and exposes retained GPU resources rather than an
immediate-mode drawing API. It currently provides five compact renderer paths:

- cheap retained quads/triangle-fan circles;
- explicit circles and rounded rectangles as signed-distance fields;
- cubes and low-poly spheres with GPU-side transforms and material effects;
- text backed by the default atlas or bounded project-local font atlases and a
  retained glyph VBO;
- project-relative image assets with cached GPU textures and retained quads.

Solid geometry and glyph families are compact GLES2 batches. Images reuse one
texture upload for every element with the same path and issue one small draw per
image so different textures can coexist. Geometry and glyph buffers are rebuilt
only when construction or retained state changes.

```js
fx.configure({
  outputWidth: 0,
  outputHeight: 0,
  pixelDensity: "auto",
  minimumPixelDensity: 0.5,
  densitySampleFrames: 180,   // adaptation cadence
  densityStep: 0.025,         // requested density step (DRM modes are discrete)
  densityDownThreshold: 1.08, // overload relative to frame budget
  densityUpThreshold: 0.72,   // spare-capacity threshold
  densityUpSamples: 4,        // consecutive samples before raising quality
  targetFps: 30,
  debugBar: true,
  durationSeconds: 0,
  quality: "performance",     // performance, balanced, or quality
  colorFormat: "rgb565",      // rgb565 or rgba8888
  antialiasing: "none",       // none or msaa4
  depthBits: 16,               // 16 or 24
  dithering: true,
  profiling: false,            // detailed timing is opt-in
  profileIntervalFrames: 120
});

const scene = fx.scenes.add(fx.scene({ name: "main" }));
const background = scene.add(fx.gradientRect(960, 540, 1920, 1080,
                                              0x142040ff, 0x080c1dff));
const dot = scene.add(fx.circle(200, 200, 40, 0xffcc33ff));
const panel = scene.add(fx.sdfRoundedRect(600, 400, 300, 120, 24, 0x202850dd));
const cube = scene.add(fx.cube(0, 1, 0, 1.2, 0xffcc33ff));
const model = scene.add(fx.model("models/icosahedron.obj", 0, 1, 0, 2.4,
                                 0x65d9ffff));
const label = scene.add(fx.text("hello GPU", 80, 160, 32, 0xffffffff,
                                "fonts/display.ttf"));
const logo = scene.add(fx.image("images/logo.png", 1600, 150, 240, 120,
                                0xffffffff));

fx.camera(0, 3, 9, 0, 1, 0, 48);
cube.effect(fx.effects.gradient, 0.9, 1.0);

function update(time, delta) {
  dot.position(200 + Math.sin(time) * 100, 200);
  cube.position(0, 1, 0).rotation(time * 0.2, time * 0.5, 0);
}
```

2D and text coordinates use the 1920×1080 design space and map to the selected
global HDMI mode. 3D uses world coordinates and radians. Colors are
`0xRRGGBBAA`. Construction belongs at script load; `update` should mutate
retained objects. `position()` sets an absolute position while `move()` adds a
relative offset. Both are fluent and return the same object.

## JavaScript API

| Constructor or method | Retained operation |
| --- | --- |
| `rect(x,y,w,h,color)` | Add a cheap solid GPU quad |
| `line(x1,y1,x2,y2,width,color)` | Add a cheap retained line as a rotated quad |
| `polyline(points,width,color[,options])` | Build an open/closed retained path from the same quad batch |
| `gradientRect(x,y,w,h,top,bottom)` | Add a cheap vertical-gradient quad |
| `background(top,bottom)` | Add a gradient rendered before the 3D scene |
| `circle(x,y,r,color)` | Add a fast retained triangle-fan circle |
| `sdfCircle(x,y,r,color)` | Add an experimental SDF circle |
| `sdfRoundedRect(x,y,w,h,r,color)` | Add an SDF rounded rectangle |
| `element.position(x,y[,z])` | Set an element's absolute position |
| `element.move(dx,dy[,dz])` | Move an element relative to its current position |
| `element.rotation(...)` / `rotate(...)` | Set/add retained rotation |
| `element.scale(size)` | Set a 3D element's retained size |
| `element.color(color)` | Change an element's color |
| `element.opacity(value)` | Set 2D/text/image opacity from 0 to 1 |
| `element.visible(value)` | Set retained visibility without reallocating |
| `element.show()` / `element.hide()` | Convenience visibility controls |
| `element.effect(kind,amount,scale)` | Select a GPU material effect |
| `element.text(value)` | Change a retained text element |
| `element.font(path)` | Select a project-local TTF/OTF font; empty resets the default |
| `cube(x,y,z,size,color)` | Add a unit-cube primitive |
| `sphere(x,y,z,size,color)` | Add a low-poly sphere |
| `wireCube(x,y,z,size,color)` | Add a retained 3D cube outline |
| `grid(x,y,z,size,color)` | Add a retained horizontal 3D grid |
| `model(path,x,y,z,size,color)` | Load a project-relative model asset |
| `image(path,x,y,w,h,tint)` | Load a project-relative image as a retained 2D quad |
| `camera(x,y,z,tx,ty,tz,fovY)` | Set retained perspective camera |
| `text(value,x,y,size,color[,fontPath])` | Add retained atlas text with an optional project font |
| `scene(options)` / `scenes.add(scene)` | Create and register an application scene |
| `group(...elements)` | Create a translation/visibility/color container without another renderer path |
| `group.add(element)` | Add one retained element; an element belongs to at most one group |
| `group.position(...)` / `move(...)` | Translate every member while preserving its retained local state |
| `scene.add(elementOrGroup)` | Record explicit membership and return the element or group |
| `configure(settings)` | Set startup output/FPS policy from the application |
| `debugBar(visible)` | Show or hide the native runtime diagnostics bar |
| `env(name,fallback)` | Read deployment information supplied to the app |
| `data(path[,fallback])` | Parse a bounded project-relative JSON asset |

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

The operation helpers `fx.move`, `fx.transform`, `fx.setText`, `fx.font`, `fx.color`,
`fx.opacity`, and `fx.effect` remain useful for data-driven code. They accept either a retained
object or its numeric `handle`; application code should normally prefer object
methods.

Groups are JavaScript-side retained containers over existing native handles.
They do not add draw calls, buffers, or a second renderer. `polyline()` validates
all points before construction and emits ordinary rotated quads, so static
paths join the existing dirty-buffer batch. Group transforms intentionally
support translation only; silently approximating hierarchical rotation or
scale would produce incorrect 2D/3D semantics. A group's opacity is available
only when all members are 2D, matching the underlying depth policy.

Opacity is retained for 2D quads, SDF shapes, text, and images. Translucent
3D meshes are intentionally not exposed yet: making them correct requires a
documented depth-write and back-to-front sorting policy rather than silently
producing order-dependent artifacts.

`configure()` runs while the script is evaluated, before DRM/GL initialization.
Both output dimensions must be zero (native/automatic) or both non-zero (fixed).
The named quality preset establishes a complete baseline, after which explicit
`colorFormat`, `antialiasing`, `depthBits`, and `dithering` values override it.
Environment variables are the final deployment override. `debugBar()` may also
be called from `update()` and takes effect on the next frame.
Automatic density uses separate overload and recovery thresholds to avoid
oscillation. It lowers at the first overloaded sample and raises only after
`densityUpSamples` consecutive under-budget samples. The five density policy
properties above are application settings; deployment overrides use the
equivalent `MICROFX_DENSITY_*` variables.
Detailed timing logs are disabled by default; set `profiling: true` for hardware
experiments. `profileIntervalFrames` controls the reporting cadence and must be
at least 30, keeping normal installations free of recurring diagnostic writes.
Each record includes the target frame budget, mean and worst wall time, count of
frames over budget, process CPU time, non-CPU/pacing time, and named render
stages. The non-CPU value is deliberately not called GPU time: GLES 2 exposes no
portable timer query here, so it also contains DRM page-flip/display waiting.
`engine/tools/profile-report.py` produces a frame-weighted summary. It never
averages different output/density/target configurations under one label: the
normal report selects the latest configuration, while `--matrix` keeps every
configuration as a separate comparison row. DRM records currently lack render
configuration metadata and are therefore attributed only when the input has
one configuration. The platform helper exposes both views as
`scripts/canvas-profile.sh HOST report` and `scripts/canvas-profile.sh HOST matrix`.
The report also exposes named-stage accounting and the EGL/GBM/DRM sub-stages
emitted by the target backend. Automated runs can use `--max-budget-use` and
`--max-over-budget` to return a failing status when a configuration crosses an
accepted percentage threshold.

The JavaScript application always owns the complete frame; the engine contains
no built-in application scene. A positive `durationSeconds` exits the
application cleanly after that interval; zero runs indefinitely. The firmware
uses this control for its portable 40-second onboarding application.

Model, image, and font assets belong beside the application script or in a directory below it.
`model()`, `image()`, and `font()` accept only a relative path. The runtime resolves both the project
directory and asset to canonical paths, then rejects absolute paths, missing or
non-regular files, `..` traversal outside the project, and symlinks that escape
the project directory. This is a basic project boundary for application assets,
not a security sandbox for untrusted JavaScript.

The text renderer caches at most four faces, including the built-in default,
and preserves scene order by batching only adjacent strings that share a face.
Font parsing and GPU upload happen once, after graphics initialization; a bad
font or a fifth distinct face is a visible fatal asset error rather than an
implicit fallback.

Current limits are 256 SDF elements, 16 mesh elements, 32 text elements of 127
bytes each, four font faces, 16 image elements/textures, a 16 MiB JS heap, and a 512 KiB JS stack. A script exception is fatal
by design so a bad remote revision remains visible through independent SSH
rather than silently changing behavior.

`data()` applies the same project boundary as `model()`, accepts at most 64 KiB,
and parses JSON inside the existing QuickJS runtime. It first checks the matching
project directory below `/run/microfx-data`, then falls back to the project's
persistent `assets/`. Platform adapters can therefore publish atomic live JSON
updates without recurring SD-card writes or giving application JavaScript
general network/filesystem access. Passing a fallback as the second argument
makes a missing asset non-fatal; malformed JSON remains a visible script error.

`apps/demo/scripts/text-benchmark.js` isolates the retained glyph path at fixed
native density. On the target GC880, three static strings retain the same
1920x1080/30 FPS tier as the native demo without JS overlays. Static text builds
one glyph VBO once and submits it in one draw; changing text rebuilds that VBO.
Large SDF-covered areas are a separate fragment-fill cost and should not be used
to infer text performance.

The public facade lives in `engine/runtime/retained.js` and is embedded into the
target binary by `engine/tools/embed-runtime.py`. Its Node tests run as part of
`engine/tests/run.sh`, so object semantics can be developed without an SD card
or target board.

Future renderer bindings must preserve this model: bounded object arrays,
static/dirty GPU buffers, atlased text, and one draw per material/batch—not one
draw per JavaScript call.
