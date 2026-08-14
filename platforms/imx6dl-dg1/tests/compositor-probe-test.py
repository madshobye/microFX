#!/usr/bin/env python3

import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "evaluate-drm-plane-probe.py"
FIXTURES = ROOT / "tests" / "fixtures"

spec = importlib.util.spec_from_file_location("drm_probe", SCRIPT)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

capable = module.parse_probe((FIXTURES / "drm-plane-overlay.txt").read_text())
assert capable["summary"] == {
    "planeCount": 2,
    "overlayPlaneCount": 1,
    "rgbOverlayPlaneCount": 1,
    "globalAlphaProperty": True,
    "zPositionProperty": True,
    "structuralCandidate": True,
    "experimentReady": True,
}
assert capable["decision"] == "native-plane-experiment"
assert capable["blockers"] == []

primary = module.parse_probe((FIXTURES / "drm-plane-primary-only.txt").read_text())
assert primary["summary"]["overlayPlaneCount"] == 0
assert primary["summary"]["experimentReady"] is False
assert primary["decision"] == "keep-gles-baseline"
assert "no imx-drm overlay plane was identified" in primary["blockers"]

# The command-line boundary must emit the same machine-readable schema.
with tempfile.TemporaryDirectory() as temporary:
    output = Path(temporary) / "report.json"
    subprocess.run(
        ["python3", str(SCRIPT), str(FIXTURES / "drm-plane-overlay.txt"), "--output", str(output)],
        check=True,
    )
    assert json.loads(output.read_text())["decision"] == "native-plane-experiment"

print("DRM plane capability evaluator tests passed")

