# pcb-toolpath-helper

Turns PCB fabrication data (Gerbers, drill file, outline) into the **project
folder** consumed by the PCB wizard.

The output contract is defined in [`docs/pcb-wizard.md`](../docs/pcb-wizard.md)
— read it first; this helper exists to satisfy it.

## Requirements

- Python 3.8+
- [FlatCAM](http://flatcam.org/) installed (`pip install flatcam` or the
  FlatCAM AppImage/Windows build), or Docker.
- FlatCAM is **not bundled** here: it is GPLv3, so we call it as an external
  tool instead of vendoring it.

## Usage

```sh
python3 flatcam_gen.py \
  --top board-F_Cu.gbr \
  --bottom board-B_Cu.gbr \
  --drill board.drl \
  --outline board-Edge_Cuts.gbr \
  --out ./projects/3018-board

# validate before uploading to the controller
python3 verify.py ./projects/3018-board
```

`flatcam_gen.py` writes a FlatCAM automation script (`run_flatcam.py`) into the
output folder and runs it, then checks the result. If FlatCAM is not found the
script is still written — run it later on a machine that has FlatCAM:

```sh
python3 ./projects/3018-board/run_flatcam.py
```

Options: `--isolation-tool 0.2` (V-bit diameter, mm), `--drill-tool 0.8`,
`--feeds/--speeds` overrides, `--dry-run`.

## Resulting folder

```
projects/3018-board/
  top.nc  bottom.nc  drills.nc  profile.nc  run_flatcam.py
```

Upload the folder to the SD card (Files page), then start the PCB wizard and
select it.

## Notes

- Copper on the bottom side is mirrored in X (the wizard expects the file in
  board coordinates; check the preview before running).
- Drill files are grouped by tool; every group gets a SmartPCB marker so the
  wizard can show the tool-change dialog.
- FlatCAM defaults are conservative; tune `--isolation-tool` and the feeds for
  your machine before cutting.
