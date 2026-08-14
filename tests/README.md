# Test architecture

`run.sh` is the portable v1 verification entry point. It deliberately tests
the platform at several boundaries instead of treating a successful firmware
compile as proof that the appliance works:

- C scene, asset, quality, and renderer-state units;
- Node tests for the retained JavaScript facade and browser Studio;
- an instrumented JavaScript application harness that enforces allocation-free
  retained updates, finite native mutations, valid handles, project-confined
  fixture data, and native capacity limits across the bundled gallery;
- protocol tests against the real C++ device command handler;
- Save & Run through the real Studio action controller, C++ command handler,
  project supervisor, and a controlled renderer process, including concurrent
  submission rejection, renderer failure, and recovery;
- portal controls through the real supervisor;
- hermetic Wi-Fi, setup-AP, watchdog, data-adapter, and A/B image policies.

`lib/microfx-test.sh` is the dependency-free POSIX interaction-test library for
firmware services. It provides labelled equality, file-content, line-count,
empty-file, temporary-directory, and completion helpers. New init scripts and
watchdogs should expose filesystem, timing, and command boundaries through
environment variables, then exercise those boundaries with controlled fixtures
instead of sleeping or touching the developer's machine.

`apps/tests/lib/runtime-test.mjs` is the reusable application-side test
library. It can load a project script or an in-memory test program, advance
deterministic frames, and return element counts plus per-frame mutation
pressure. The gallery gate uses it for a ten-minute simulated stress pass per
application; focused unit tests prove that the harness itself detects dynamic
GPU allocation, invalid numeric state, capacity overflow, and asset traversal.

`protocol-contract-test.py` treats `services/peer-bridge/protocol.json` as the
machine-readable command inventory. It fails when the real C++ handler adds or
removes a command without updating the contract, when Studio sends an unknown
command, when the read-only interaction check drifts from its declared safe
set, or when a command is missing from the real-handler integration test.

Hardware-only behavior is tracked separately in
`platforms/imx6dl-dg1/HARDWARE-VALIDATION.md`.
