#!/usr/bin/env python3
"""Summarize and compare microFX renderer and DRM timing records from stdin."""

from __future__ import annotations

import argparse
import json
import re
import sys

PAIR = re.compile(r"([A-Za-z_]+)=([^\s]+)")


def records(lines, prefix):
    result = []
    for line in lines:
        marker = prefix + " "
        position = line.find(marker)
        if position < 0:
            continue
        values = dict(PAIR.findall(line[position + len(marker) :]))
        result.append(values)
    return result


def number(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def weighted(rows, weight_name, fields):
    total = sum(max(number(row.get(weight_name)), 0.0) for row in rows)
    if total <= 0:
        return {field: 0.0 for field in fields}, 0.0
    return {
        field: sum(number(row.get(field)) * max(number(row.get(weight_name)), 0.0)
                   for row in rows) / total
        for field in fields
    }, total


def configuration(row):
    return (row.get("output", "unknown"), number(row.get("density")),
            int(number(row.get("target_fps"))))


def summarize_rows(profile, drm):
    profile_fields = ("fps", "script", "begin", "background", "mesh", "overlay",
                      "interface", "present", "cpu", "noncpu", "wall")
    averages, frames = weighted(profile, "frames", profile_fields)
    drm_fields = ("egl", "lock", "addfb", "wait_previous", "flip_submit",
                  "post_submit", "total")
    drm_average, swaps = weighted(drm, "swaps", drm_fields)
    over_budget = sum(int(number(row.get("over_budget"))) for row in profile)
    max_wall = max((number(row.get("max_wall")) for row in profile), default=0.0)
    latest = profile[-1] if profile else {}
    budget = number(latest.get("budget"))
    wall = averages["wall"]
    stage_names = ("script", "begin", "background", "mesh", "overlay",
                   "interface", "present")
    accounted = sum(averages[name] for name in stage_names)
    dominant = max(stage_names, key=lambda name: averages[name]) if profile else "none"
    return {
        "samples": len(profile),
        "frames": int(frames),
        "output": latest.get("output", "unknown"),
        "density": number(latest.get("density")),
        "targetFps": int(number(latest.get("target_fps"))),
        "budgetMs": budget,
        "budgetUsePercent": (wall * 100.0 / budget) if budget else 0.0,
        "headroomMs": budget - wall if budget else 0.0,
        "accountedStageMs": accounted,
        "unaccountedMs": max(wall - accounted, 0.0),
        "dominantStage": dominant,
        "averageMs": averages,
        "maxWallMs": max_wall,
        "overBudgetFrames": over_budget,
        "overBudgetPercent": (over_budget * 100.0 / frames) if frames else 0.0,
        "drmSamples": len(drm),
        "drmSwaps": int(swaps),
        "drmAverageMs": drm_average,
    }


def summarize_groups(lines):
    source = list(lines)
    profile = records(source, "MICROFX_PROFILE")
    drm = records(source, "DRM_TIMING")
    groups = {}
    for row in profile:
        groups.setdefault(configuration(row), []).append(row)
    # DRM records do not currently carry output/density metadata. They are
    # attributable only when the input contains one renderer configuration.
    one_configuration = len(groups) == 1
    return [summarize_rows(rows, drm if one_configuration else [])
            for rows in groups.values()]


def summarize(lines):
    source = list(lines)
    groups = summarize_groups(source)
    if groups:
        result = groups[-1]
        result["configurations"] = len(groups)
        result["drmAttribution"] = "single-configuration" if len(groups) == 1 else "mixed-unavailable"
        return result
    result = summarize_rows([], records(source, "DRM_TIMING"))
    result["configurations"] = 0
    result["drmAttribution"] = "no-renderer-records"
    return result


def print_summary(summary):
    print(f"microFX profile: {summary['frames']} frames, {summary['output']} @ density {summary['density']:.3f}")
    print(f"target {summary['targetFps']} fps / {summary['budgetMs']:.3f} ms; "
          f"average {summary['averageMs']['wall']:.3f} ms; max {summary['maxWallMs']:.3f} ms")
    print(f"budget use {summary['budgetUsePercent']:.1f}%; headroom {summary['headroomMs']:.3f} ms; "
          f"over budget {summary['overBudgetFrames']} frames ({summary['overBudgetPercent']:.1f}%)")
    print(f"CPU {summary['averageMs']['cpu']:.3f} ms; non-CPU/pacing {summary['averageMs']['noncpu']:.3f} ms; "
          f"present {summary['averageMs']['present']:.3f} ms")
    print(f"stages account for {summary['accountedStageMs']:.3f} ms; "
          f"unaccounted {summary['unaccountedMs']:.3f} ms; dominant {summary['dominantStage']}")
    if summary["drmSamples"]:
        drm = summary["drmAverageMs"]
        print(f"DRM egl {drm['egl']:.3f} ms; lock {drm['lock']:.3f} ms; addfb {drm['addfb']:.3f} ms; "
              f"wait_previous {drm['wait_previous']:.3f} ms; submit {drm['flip_submit']:.3f} ms; "
              f"post_submit {drm['post_submit']:.3f} ms")


def print_matrix(groups):
    print("output       density target frames    fps    wall     cpu present  budget%   over%")
    for summary in groups:
        average = summary["averageMs"]
        print(f"{summary['output']:<12} {summary['density']:>7.3f} "
              f"{summary['targetFps']:>6} {summary['frames']:>6} "
              f"{average['fps']:>6.1f} {average['wall']:>7.3f} "
              f"{average['cpu']:>7.3f} {average['present']:>7.3f} "
              f"{summary['budgetUsePercent']:>8.1f} {summary['overBudgetPercent']:>7.1f}")


def threshold_violations(groups, max_budget_use=None, max_over_budget=None):
    violations = []
    for summary in groups:
        label = f"{summary['output']} density={summary['density']:.3f} target={summary['targetFps']}"
        if max_budget_use is not None and summary["budgetUsePercent"] > max_budget_use:
            violations.append(
                f"{label}: budget use {summary['budgetUsePercent']:.1f}% > {max_budget_use:.1f}%")
        if max_over_budget is not None and summary["overBudgetPercent"] > max_over_budget:
            violations.append(
                f"{label}: over-budget frames {summary['overBudgetPercent']:.1f}% > {max_over_budget:.1f}%")
    return violations


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--matrix", action="store_true",
                        help="keep output/density/target configurations separate")
    parser.add_argument("--max-budget-use", type=float, metavar="PERCENT")
    parser.add_argument("--max-over-budget", type=float, metavar="PERCENT")
    args = parser.parse_args()
    if ((args.max_budget_use is not None and args.max_budget_use < 0) or
            (args.max_over_budget is not None and args.max_over_budget < 0)):
        parser.error("performance thresholds must not be negative")
    lines = sys.stdin.readlines()
    groups = summarize_groups(lines)
    summary = summarize(lines)
    if args.json:
        json.dump({"configurations": groups} if args.matrix else summary,
                  sys.stdout, indent=2, sort_keys=True)
        print()
    elif args.matrix:
        print_matrix(groups)
    else:
        print_summary(summary)
    violations = threshold_violations(
        groups if args.matrix else ([summary] if summary["frames"] else []),
        args.max_budget_use, args.max_over_budget)
    for violation in violations:
        print(f"performance threshold failed: {violation}", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
