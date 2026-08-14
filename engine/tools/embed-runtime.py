#!/usr/bin/env python3
"""Embed a JavaScript runtime source file as a C string constant."""

from pathlib import Path
import json
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: embed-runtime.py INPUT.js OUTPUT.inc", file=sys.stderr)
        return 2

    source_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    source = source_path.read_text(encoding="utf-8")
    lines = ["/* Generated file. Edit engine/runtime/retained.js instead. */"]
    lines.append("static const char *MICROFX_RUNTIME_JS =")
    for line in source.splitlines(keepends=True):
        lines.append(f"    {json.dumps(line)}")
    if source and not source.endswith("\n"):
        lines[-1] += "\n"
    lines.append("    ;")
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
