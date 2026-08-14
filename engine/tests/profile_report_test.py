#!/usr/bin/env python3
import importlib.util
from pathlib import Path

tool = Path(__file__).parents[1] / "tools" / "profile-report.py"
spec = importlib.util.spec_from_file_location("profile_report", tool)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

mixed_lines = [
    "MICROFX_PROFILE frames=100 output=1920x1080 density=1.000 fps=30 target_fps=30 budget=33.333 script=1 begin=2 background=3 mesh=4 overlay=5 interface=1 present=16 cpu=12 noncpu=21 wall=33 max_wall=40 over_budget=10\n",
    "MICROFX_PROFILE frames=20 output=1280x720 density=0.667 fps=30 target_fps=30 budget=33.333 script=2 begin=2 background=2 mesh=2 overlay=2 interface=2 present=18 cpu=10 noncpu=26 wall=36 max_wall=44 over_budget=5\n",
    "DRM_TIMING swaps=120 egl=0.7 lock=0.4 addfb=0.1 wait_previous=12 flip_submit=0.2 post_submit=0.3 total=13.7\n",
]
summary = module.summarize(mixed_lines)

assert summary["frames"] == 20
assert summary["output"] == "1280x720"
assert summary["targetFps"] == 30
assert summary["configurations"] == 2
assert summary["drmAttribution"] == "mixed-unavailable"
assert summary["overBudgetFrames"] == 5
assert abs(summary["overBudgetPercent"] - 25.0) < 0.001
assert summary["maxWallMs"] == 44
assert abs(summary["averageMs"]["wall"] - 36.0) < 0.001
assert abs(summary["budgetUsePercent"] - 108.00108) < 0.001
assert abs(summary["headroomMs"] + 2.667) < 0.001
assert summary["accountedStageMs"] == 30
assert summary["unaccountedMs"] == 6
assert summary["dominantStage"] == "present"
assert summary["drmSwaps"] == 0

groups = module.summarize_groups(mixed_lines)
assert len(groups) == 2
assert groups[0]["output"] == "1920x1080"
assert groups[0]["frames"] == 100
assert groups[0]["overBudgetFrames"] == 10
assert groups[1]["output"] == "1280x720"
assert groups[1]["frames"] == 20

single = module.summarize([
    mixed_lines[0],
    "MICROFX_PROFILE frames=20 output=1920x1080 density=1.000 fps=29 target_fps=30 budget=33.333 script=2 begin=2 background=2 mesh=2 overlay=2 interface=2 present=18 cpu=10 noncpu=26 wall=36 max_wall=44 over_budget=5\n",
    mixed_lines[2],
])
assert single["frames"] == 120
assert single["configurations"] == 1
assert single["drmAttribution"] == "single-configuration"
assert abs(single["averageMs"]["wall"] - 33.5) < 0.001
assert single["drmSwaps"] == 120
assert single["drmAverageMs"]["egl"] == 0.7
assert single["drmAverageMs"]["lock"] == 0.4
assert single["drmAverageMs"]["addfb"] == 0.1
assert single["drmAverageMs"]["wait_previous"] == 12
assert module.threshold_violations(groups, max_budget_use=105) == [
    "1280x720 density=0.667 target=30: budget use 108.0% > 105.0%"
]
assert len(module.threshold_violations(groups, max_over_budget=20)) == 1
assert module.threshold_violations(groups, max_budget_use=110, max_over_budget=30) == []
print("profile report tests passed")
