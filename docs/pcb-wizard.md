# PCB wizard — project folder contract

This is the interface between the **toolpath helper** (`pcb-toolpath-helper/`,
runs on a PC with FlatCAM) and the **PCB wizard** in the web UI (runs on the
controller). The wizard does not generate G-code; it consumes a project folder
that matches this contract.

## Folder layout

The project folder lives on the controller's SD card (`/sdcard/<project>/`).
Phase files are read in this fixed order: **top → flip → bottom → drills → profile**.

```
<project>/
  top.nc        top copper isolation (required)
  flip.nc       optional: board flip / registration moves
  bottom.nc     bottom copper isolation (mirrored in X)
  drills.nc     all drills, grouped by tool (required for drilling jobs)
  profile.nc    board outline / cutout (required for profile jobs)
  project.json  optional metadata (name, material, tool table)
```

Rules:

- Extensions: `.nc` is canonical; `.gcode` / `.tap` are accepted as aliases.
- Units: **mm**, `G21` present in the header.
- Distance mode: absolute `G90`.
- Feed mode: `G94` (units/min).
- Coordinates are relative to the work origin touched at wizard step 2;
  Z=0 is the probed surface (heightmap compensation applied by the controller).
- File names are case-insensitive; the phase list is fixed, extra files are ignored.

## Tool-change markers (SmartPCB)

The wizard builds its tool-change dialogs from the G-code. A tool change is a
`T<n>` / `M6` followed by a comment in this exact form:

```
(MSG, Change to Tool Dia = 0.8000 ||| Total drills for tool T1 = 64)
```

- `Dia` is the tool diameter in **mm** (used for the dialog title).
- `Total`/`tool` is informational (drill count for that tool).
- Files without markers still run; the wizard then asks for a manual tool change.

## Validation (wizard step 1)

When the folder is selected the wizard:

1. Lists the phase files it found and flags missing required phases.
2. Rejects files that are empty or not plain text.
3. Warns if `G21`/`G90` are missing.
4. Reports the total number of tool changes parsed from the markers.

The same checks are implemented in `pcb-toolpath-helper/verify.py`, so the
helper can validate its output before it is uploaded.

## Example

```
3018-board/
  top.nc        12,345 bytes
  bottom.nc      9,876 bytes
  drills.nc      3,210 bytes   (+ 2 tool markers)
  profile.nc     1,234 bytes
```
