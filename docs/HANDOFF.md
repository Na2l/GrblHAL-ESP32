# Handoff / current state (DLC32 MAX, 3018-Pro)

Last updated: 2026-11-25. Branch: `3018-pro` of
https://github.com/Na2l/GrblHAL-ESP32 (fork of `grblHAL/ESP32`).

## Board & network

- MKS DLC32 MAX V1.0/V1.2, ESP32-S3 rev v0.2, 8 MB flash, 4 axes (X,Y,Z,A).
- WiFi STA `192.168.1.66` (SSID/password stored in the controller's NVS,
  **not** in this repo). AP fallback `grblHAL_AP` / `192.168.5.1`.
- Running: grblHAL ESP32 driver + our UI; A/B OTA (`ota_0`/`ota_1`,
  oradata states VALID), health gate + rollback enabled.
- OTA endpoint: `POST http://<ip>:8080/ota` with `X-OTA-Password`.

## Secrets

- OTA password is **not** in the repo: `main/ota_secret.h` (gitignored),
  generate with `python3 scripts/make_ota_secret.py`.
- WiFi credentials: set via `$74`/`$75` or `tools/set_wifi.py`.

## Current controller settings (snapshot)

See `docs/board-settings-20261125.json` (full `/api/settings` dump). Summary:

| Axis | steps/mm | max rate | accel mm/s2 | travel mm |
|---|---|---|---|---|
| X | 796.79 | 800 | 100 | 300 |
| Y | 798.45 | 800 | 100 | 180 |
| Z | 641.03 | 800 | 200 | 200 (modified Z, measure again) |
| A | 250 | 500 | 10 | 200 |

- `$3=7` (X/Y/Z direction inverted), `$5=15` (limit polarity, NO switches
  to GND), hard limits ON, soft limits OFF, homing ON.
- Homing: dir mask 0, pull-off 2 mm, feed 100, seek 2000, debounce 250.
- Machine homes at the max end; work moves are negative.
- Home switches are at the min end after inversion config; both Z switches
  are wired to the Z input (parallel).

## Build / flash

```sh
git clone -b 3018-pro --recursive https://github.com/Na2l/GrblHAL-ESP32.git
cd GrblHAL-ESP32
python3 scripts/patch_submodules.py     # N_AXIS 4 + ftpd SD-mount fix
python3 scripts/make_ota_secret.py 'pw' # if you need to OTA the same board
pio run -e dlc32-max
# app: .pio/build/dlc32-max/dlc32-max.bin
```

- Recovery: `esptool.py --chip esp32s3 -p /dev/ttyUSB0 write_flash 0x10000 dlc32-max.bin`
  (or full image at `0x0`; stock 8 MB image + full dump in `backups/`, not in git).
- A/B: keep the other slot on a known-good build; `$RS` reports slots,
  `$RB` rolls back.

## Known issues / notes

- `docs/screens/` screenshots are partly **stale** (some show old breadcrumbs,
  old diagram, old wizard buttons). Regenerate after flow validation.
- Job page is a placeholder: SD job streaming (P2) not implemented yet.
- Heightmap import/export and per-project persistence: not implemented.
- Wizard job flows (PCB/flatten/run) are not implemented; setup and calibration
  wizards are.
- `Na2l/3018-Pro` still has an older `dlc32max-ui` branch (backup only).

## Next steps

1. Re-capture screenshots into `docs/screens/` and update `docs/README.md`.
2. P2: SD job streaming (start/pause/stop, progress), then wizard run flows.
3. Heightmap import/export + per-project persistence.
4. Fork `grblHAL/core` and `grblHAL/Plugin_networking`, apply `patches/*.patch`,
   open PRs; then drop `scripts/patch_submodules.py`.
5. Rotate the OTA password (it was exposed in an early commit, history was
   rewritten; rotation makes it moot).
