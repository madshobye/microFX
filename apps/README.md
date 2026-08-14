# Bundled projects

microFX ships eleven small retained-mode projects. `demo` is the default fallback;
the other folders are independent examples installed into the same persistent
project library on first boot. Each project consists of `main.js`,
`project.json`, and an optional `assets/` directory.

The gallery deliberately spans typography, 2D composition, particles,
wireframe and solid 3D, diagnostic gradients, clocks, and data-visualization
concepts. `scene-switcher` demonstrates per-frame `scene.show()` selection and
disabled-element culling. `flight-board` and `energy-clock` read bounded live
snapshots through the experimental `fx.feed()` file helper. These two feeds are
examples rather than the networking architecture. Applications can use the
runtime-owned `fetch()` and `fx.net` HTTP, TCP, UDP, and JavaScript web-server
APIs directly.

Run `apps/tests/run.sh` to verify project count, JavaScript syntax, metadata,
the public/private API boundary, and the retained runtime contract without
target hardware. The reusable `apps/tests/lib/runtime-test.mjs` harness wraps
the public facade with an instrumented native boundary. It rejects unknown
handles, non-finite transforms, capacity overflow, project-escaping fixture
paths, and any GPU-object construction from `update()`.

The default gallery stress pass advances every selectable project and the
fallback demo through 18,000 frames: ten simulated minutes at 30 FPS. It also
reports average and maximum native mutations per frame. Use
`MICROFX_APP_TEST_FRAMES` to request a longer deterministic soak without
changing an application or the target image.

Native initialization, the render loop, and renderer dispatch belong to
`engine/runtime/`; application folders must not contain C or C++ sources.
