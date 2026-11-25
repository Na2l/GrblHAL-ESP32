#!/usr/bin/env python3
"""Validate a project folder against docs/pcb-wizard.md before uploading it.

    python3 verify.py ./projects/3018-board
"""
import os
import re
import sys

PHASES = ["top", "flip", "bottom", "drills", "profile"]
EXTS = (".nc", ".gcode", ".tap")
REQUIRED = ("top",)
MARKER_RE = re.compile(r"\(MSG,\s*Change to Tool Dia\s*=\s*([0-9.]+)[^)]*\)", re.I)


def find_phase(folder, phase):
    for name in os.listdir(folder):
        stem, ext = os.path.splitext(name)
        if ext.lower() in EXTS and stem.lower() == phase:
            return os.path.join(folder, name)
    return None


def check_file(path):
    problems = []
    try:
        text = open(path, "r", encoding="utf-8", errors="replace").read()
    except OSError as exc:
        return [f"cannot read: {exc}"], 0, ""
    if not text.strip():
        problems.append("file is empty")
    if "G21" not in text.upper():
        problems.append("missing G21 (units: mm)")
    if "G90" not in text.upper():
        problems.append("missing G90 (absolute mode)")
    markers = MARKER_RE.findall(text)
    return problems, len(markers), text


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    if not os.path.isdir(folder):
        sys.exit(f"not a folder: {folder}")

    print(f"project: {os.path.abspath(folder)}")
    found, errors = {}, []
    for phase in PHASES:
        path = find_phase(folder, phase)
        if not path:
            if phase in REQUIRED:
                errors.append(f"missing required phase: {phase}.nc")
            continue
        problems, tool_changes, _ = check_file(path)
        found[phase] = path
        size = os.path.getsize(path)
        status = "ok" if not problems else "WARN"
        print(f"  {phase:8s} {os.path.basename(path):14s} {size:>8,d} B  "
              f"tool changes: {tool_changes:<3d} {status}")
        for p in problems:
            print(f"           - {p}")

    if not found:
        errors.append("no phase files found (.nc/.gcode/.tap)")

    if errors:
        print("\nFAILED:")
        for e in errors:
            print("  -", e)
        return 1
    print("\nOK - folder matches the PCB wizard contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
