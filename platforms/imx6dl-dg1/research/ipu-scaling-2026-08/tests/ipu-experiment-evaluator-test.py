#!/usr/bin/env python3

import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "evaluate-ipu-experiment.py"
FIXTURES = ROOT / "tests" / "fixtures"

spec = importlib.util.spec_from_file_location("ipu_experiment", SCRIPT)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

success = module.parse_experiment((FIXTURES / "ipu-ic-experiment-success.txt").read_text())
assert success["viable"] is True
assert success["decision"] == "ipu-ic-prototype-viable"
assert success["profile"]["fps"] == 30.033
assert success["profile"]["processCpuPercent"] == 5.265
assert success["blockers"] == []

failed = module.parse_experiment((FIXTURES / "ipu-ic-experiment-failed.txt").read_text())
assert failed["viable"] is False
assert failed["decision"] == "keep-gles-baseline"
assert any("ipuIcCompositionTest" in blocker for blocker in failed["blockers"])
assert any("sustained fps" in blocker for blocker in failed["blockers"])

with tempfile.TemporaryDirectory() as temporary:
    output = Path(temporary) / "report.json"
    subprocess.run(
        [
            "python3",
            str(SCRIPT),
            str(FIXTURES / "ipu-ic-experiment-success.txt"),
            "--output",
            str(output),
        ],
        check=True,
    )
    assert json.loads(output.read_text())["viable"] is True

print("IPU experiment evaluator tests passed")
