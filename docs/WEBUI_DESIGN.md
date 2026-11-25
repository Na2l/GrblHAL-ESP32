# WebUI — frozen IA & implementation spec (v6 approved)

Board: MKS DLC32 MAX (ESP32-S3, 8 MB, A/B OTA). Firmware: grblHAL ESP32 driver.
Status: design approved 24 Nov 2026. Firmware: P1 in progress.

## Decisions

- Replace the stock ESP3D-WebUI page with our own UI served by our own
  `esp_http_server` on **port 80** (single origin, no dead links, no CDN).
- Assets: one self-contained `ui/index.html` (HTML+CSS+JS, no external
  resources) embedded in the app image; icons = the **legacy 1-bit firmware
  bitmaps** (PCB / Flatten / Simple / Probe / Wand) converted to PNG and
  inlined as data URIs.
- Chrome icons: curated inline SVG (no Font Awesome font file).
- OTA stays bulletproof (A/B + rollback, password header) and is also exposed
  on the UI origin as `/api/fw`; port 8080 server kept as recovery path.
- Heightmap: own page, reached from **Wizards** (not a main-page tile) and
  from wizard step 4. Actions: Probe / Import / Export / Clear.
  Values shown **relative to work Z0**. 3D surface + numeric matrix; on
  phones 3D/Matrix toggle (one at a time).
- Wizards: icon-only grid; each tile opens its **own page** with a step flow
  (PCB: Project → Setup → Flatten → Heightmap → Run).
- Main page tiles: Move · Wizards · Files · Job · Console · Settings
  (+ active-job strip). Settings absorbs Diagnostics (former "Tools") and
  Firmware & About.
- Move: Home / Zero XY / Zero Z / Stop on top; XY pad with a step dropdown in
  the centre; Z column with its own step dropdown; independent XY/Z steps.
- Resume (wizard grid) uses a refresh icon.
- Values in the heightmap matrix: relative to Z0.

## Screens

1. Main: status strip (state, pins, DRO), tile grid, active-job strip.
2. Move: actions, XY pad + step, Z + step, spindle/coolant, hold-to-jog.
3. Wizards: icon grid (PCB, Flatten, Simple, Heightmap, Resume).
4. PCB wizard page: 5 steps, back arrow, project table, upload.
5. Heightmap: 3D surface, numeric matrix, legend, Probe/Import/Export/Clear,
   stats (min/max/delta), per-project persistence.
6. Files: SD browser, upload, delete, free space.
7. Console: terminal (send + captured output), heightmap log, alarms.
8. Settings: Machine, Probe & heightmap, Spindle & coolant, WiFi & network,
   Security, Diagnostics, Backup/restore, Firmware & About.
9. Job: progress, pause/resume/stop, phase list (PCB), last job.

## HTTP API (P1)

```
GET  /                    -> ui/index.html
GET  /api/status          -> {state, x,y,z,a, feed, spindle, pins, sd, ip}
GET  /api/info            -> versions, slot, plugins
POST /api/command         -> body: raw line (G-code or $ command)
POST /api/realtime?c=XX   -> single realtime char (hex), e.g. 21=! 7e=~ 18=reset
POST /api/jog?axis=x&d=10&feed=1000
POST /api/zero?axis=xy|z
POST /api/home
GET  /api/files?path=/
POST /api/files?path=/name.nc   (raw body -> SD)
DELETE /api/files?path=/name.nc
GET  /api/heightmap       -> {state, n, x0,y0,dx,dy, wco_z, z[][] } (relative Z0)
POST /api/heightmap?action=probe|clear
GET  /api/fw/status       -> {ok, running, boot, next, version, state}
POST /api/fw              -> app-only .bin, header X-OTA-Password
```

Snapshot model: a task in the grbl task context updates a volatile snapshot
~10 Hz (state, positions, pins, feed, spindle, wco_z); HTTP handlers only read
it (no cross-core access to grbl state).

## Implementation phases

- P1 (now): ui.c server + embedded page; Main, Move, Files, Console, Settings/
  Firmware, Heightmap (probe/clear/status, 3D+matrix once ready).
  Old ESP3D WebUI disabled; OTA 8080 kept for recovery.
- P2: Wizards pages + step flows; job streaming from SD (pause/stop/progress);
  per-probe-point events over WebSocket.
- P3: Import/Export of heightmap files; per-project persistence; settings
  write pages; WiFi config page; auth token; mobile polish.

## Notes / caveats

- Commands are enqueued via `grbl.enqueue_gcode()` (IDLE/ALARM contexts);
  realtime chars via `protocol_enqueue_realtime_command()`.
- No motion is triggered by the UI while a job streams (guards server-side).
- Heightmap probe requires probe connected + not triggered (already enforced
  by the heightmap plugin).
- Icons: legacy bitmaps for wizard tiles; inline SVG for chrome.
