# grblHAL port — consolidated plan & status (ESP32-S3 / MKS DLC32 MAX)

Everything agreed in the review threads, in one place. Keep this file updated
when scope/status changes.

Board: MKS DLC32 MAX v1.0, ESP32-S3 rev v0.2, 8 MB flash, 4 axes (X/Y/Z/A),
CH340 USB-serial. Firmware tree: `cncHAL-esp32` (driver) + `main/grbl` (core,
submodule, `N_AXIS=4` patched in `config.h`).

## Decisions (locked)

- Target firmware = grblHAL on the ESP32-S3. The Arduino `3018-Pro` firmware is
  legacy; the earlier heightmap reference fix there is standalone.
- Jobs are stored on the **SD card** and streamed by the device (WebUI uploads
  files first, then starts the job). No PC-sender autolevelling => plugin-level
  compensation is enough; planner-level (core fork) is not needed.
- OTA = **push** (browser/UI uploads the app-only `.bin`), bulletproof:
  A/B slots + otadata, image validation, rollback, never flash during a job.
- Heightmap = **fail-closed**: no partial maps, no sentinel values, never a
  compensated move unless the map is complete.
- Heightmap stores **machine Z** per point; offset = `surface_mach_z - wco_z`.
  (This removes the old Arduino "first probe vs Z0" bias and tolerates Z0
  changes; only X/Y zero changes invalidate the grid.)
- Dual-core: core 0 = WiFi/lwIP/WebUI/OTA/telemetry; core 1 = grbl task
  (`GRBLHAL_TASK_PRIORITY 5`) + step-timer ISR. New tasks pinned to core 0.
- UI: new wizard-like WebUI (mimics the Arduino wizard pages), browser-side
  heavy lifting. During file transfers nothing else runs (accepted).
- Heightmap progress = **one event per probe point**.
- G-code comments: keep the SmartPCB generator marker + range/line comments;
  fall back to computing extents by scanning the file when absent.

## Workstreams

### 1. Heightmap (core feature)
Done (v0.1, build-verified, unit-tested on host):
- `main/heightmap.{c,h}`: state machine (empty/probing/ready/invalid), N×N
  grid (max 9), machine-Z storage, bilinear interpolation + edge clamp,
  fail-closed validity, checksum. Host tests: 19/19 pass.
- `main/heightmap_plugin.c`: `M102` probe grid (blocking, like a canned cycle,
  uses core `mc_probe_cycle`/`mc_line`), `M103` report, `M104` clear;
  preconditions (idle, no stream, probe connected/not triggered);
  per-point `[MSG]`; aborts clear the map; spindle/coolant forced off.
- Enabled via `HEIGHTMAP_ENABLE` in `my_machine.h`.

TODO:
- [ ] Compensated streaming: hook `grbl.on_file_open` + custom
      `stream_read_ptr`, rewrite Z per line using `heightmap_offset()`.
      Track **raw modal Z separately** (avoid the Arduino double-compensation
      bug), preserve F/comments/all words, segment long cuts by cell/2,
      reject/abort on G91 and arcs (or handle arcs explicitly), stop the job
      on any unsupported construct. Handle `on_file_end` (close file, restore
      stream).
- [ ] Settings: grid N, probe span, probe feed, travel, retract, max
      correction via `Setting_UserDefined_450..459` + NVS (`settings_register`).
- [ ] Persistence: map per project, key = SD folder name + file size/CRC +
      generator/version; invalidate when X/Y zero changes.
- [ ] Probe-retry policy: bounded retries, configurable travel, clear abort
      message (currently one failure => alarm + cleared map).
- [ ] WebUI: heightmap page (start/cancel probe, live per-point events,
      min/max/delta display, enable/disable compensation, clear).
- [ ] SmartPCB comment guard: firmware requires/validates generator marker
      (version, extents, line count, role: top/bottom/drills/profile) and
      falls back to computing extents by scanning the file.

### 2. OTA (A/B push)
Done and **hardware-tested** (23 Nov 2026):
- `partitions_s3_8m_ota.csv`: nvs / otadata / phy_init / ota_0 (1.75 MB) /
  ota_1 (1.75 MB) / grbl (4K NVS) / storage (4.43 MB). Rollback enabled
  (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).
- `main/ota.{c,h}`: own HTTP server (port 8080, pinned to core 0):
  `POST /ota` (auth header `X-OTA-Password`, machine-idle + not-streaming
  guard, Content-Length vs slot, header validation: image magic 0xE9,
  chip id 9 (ESP32-S3), app descriptor magic 0xABCD5432, project "grbl",
  streamed 4 KB chunks, `esp_ota_end` verification, boot switch, reboot) and
  `GET /ota/status` (running/boot/next partition, version, rollback state).
  Failure => `esp_ota_abort`, running slot and boot pointer untouched.
  Health gate: new image confirms itself after 10 s, otherwise rollback.
- App 949,040 B = 51.7 % of the 1.75 MB slot.

TODO:
- [x] One-time USB reflash with the new partition table + firmware.
- [x] Hardware tests: status endpoint, 401 wrong password, rejection of
      bootloader/merged images, successful push (slot switch), settings
      preserved across OTA, rollback (reset before health confirmation ->
      bootloader fell back to the previous slot).
- [x] Recovery hardening (24 Nov): `CONFIG_ESP_TASK_WDT_PANIC=y`,
      RTC boot-loop counter (3 boots -> auto rollback), `$RB` (rollback),
      `$RS` (report slots), BOOT button held 3 s while idle -> rollback,
      per-slot reporting in `/api/fw/status` + Settings page, UI rollback button.
      Rollback verified end-to-end (ota_0 -> ota_1 -> redeploy ota_0).
      Golden-slot policy: OTA only to the inactive slot; USB flashing of the
      running slot consumes the fallback.
- [ ] Remaining: power cut during write, disconnect mid-upload, update
      while a job is streaming (guard exists, not exercised).
- [ ] Untested: BOOT-hold rollback (needs physical press), boot-loop
      auto-rollback (needs a crashing image).
- [ ] OTA page in the new WebUI (upload + progress + status/rollback report).
- [x] OTA password rotated and verified (old rejected 401, new accepted).
- [ ] Keep previous known-good `.bin` on SD for USB recovery.

### 3. Dual-core / telemetry / WebSocket
Design agreed, not implemented:
- Telemetry task pinned core 0 (priority 3-4): snapshot (state, WPos/MPos,
  feed/spindle, job %, probe progress) at 10-20 Hz, push via WebSocket;
  drop frames for slow clients; bounded buffers; no allocation per frame.
- grbl task owns all core state; other tasks only enqueue commands / read
  snapshots (seqlock or double buffer).
- Per-probe-point events from the probing loop.
- Not parallelized: flash writes during motion (OTA/files while cutting),
  grbl core state, anything above priority 5 on core 1.
- Budget: telemetry 4 KB stack + 4-8 KB ring, streamer 6-8 KB; device free
  heap ~230 KB (no PSRAM on this module).

### 4. New WebUI (wizard-like) — P1 implemented
- Static assets in `storage` partition (4.43 MB) or SD; served gzip; browser
  does rendering. No device RAM cost for UI itself.
- Endpoints needed: project list/save (SD), file upload, job start/stop/pause,
  heightmap start/cancel/status/events, OTA upload/status, settings.
- Wizard flow to mirror Arduino pages: select wizard (PCB / Flatten / Simple
  job) -> folder -> file -> jog/zero -> probe/heightmap -> run -> progress.
- During transfers assume no other activity (accepted).

### 5. SmartPCB generator (Python side)
- [ ] Emit header marker: generator + version, role (top/bottom/drills/
      profile), x/y ranges, line count, board/project id.
- [ ] Keep `(X range:)/(Y range:)/(NumOfLines:)` compatible for the old
      firmware; new firmware validates marker and cross-checks ranges.
- [ ] Per-project key material (folder + file hash) for heightmap persistence.

### 6. Testing
- Host unit tests (already: heightmap module).
- grblHAL Simulator runs of real projects (`cnc-out/`, `cnc-1s/`,
  `test_proj/`), diffing emitted Z.
- Probe/heightmap dry run (dial indicator/pen), then scrap PCB.
- OTA power-cut/rollback matrix.
- Regression: WiFi/WebUI/SD, 4-axis jog, existing G-code.

## Bring-up log (24 Nov 2026)

- Deferred OTA HTTP server start: esp_http_server crashed when started from
  driver_init because lwIP/tcpip was not up yet -> start from
  `task_run_on_startup` with retries.
- Health gate fixed: image state is NEW (not only PENDING_VERIFY) after OTA,
  so mark valid unless already VALID.
- Bootloader rollback was silently disabled: PlatformIO passed a relative
  sdkconfig path, the bootloader subproject resolved it against its own build
  dir and built from IDF defaults. Fixed with
  `board_build.esp-idf.sdkconfig_path = ${sysenv.PWD}/sdkconfig.dlc32_max`
  and a clean build; bootloader config now has
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1`.
- Probe polarity: probe to GND = trigger; `$6=1` makes "open" read
  not-triggered (verified via `Pn:`).
- Heightmap `M103` (report) and `M104` (clear) verified on hardware.
  `M102` (probe grid) not run yet - needs the work area clear.
- WiFi STA re-joined (192.168.1.66); OTA endpoint on port 8080.

## Current blockers

- Board not on USB (CH340 absent since 22:34) -> cannot flash/test the
  heightmap + OTA builds. Reconnect/power-cycle the board.
- Probe wiring/invert (`$6`) and grid parameters (N, span) need confirmation
  before the first M102 probe.
- OTA password must be chosen before deployment.
