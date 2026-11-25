# DLC32 MAX / 3018-Pro branch

This branch is **`3018-pro`** of [github.com/Na2l/GrblHAL-ESP32](https://github.com/Na2l/GrblHAL-ESP32),
a fork of the upstream **ESP32 grblHAL driver**:

- Upstream: https://github.com/grblHAL/ESP32
- Base commit: `62cf39e` (upstream `master`)
- This branch adds: built-in web UI, heightmap probing, A/B OTA, machine
  setup/calibration wizards and the MKS DLC32 MAX 4-axis build.
- Hardware: MKS DLC32 MAX V1.0/V1.2 (ESP32-S3, 8 MB flash),
  machine: 3018-Pro with modified Z axis and lead screws.

## What was added

| Area | Files | Notes |
|---|---|---|
| Web UI (port 80) | `main/ui.c`, `main/ui.h`, `main/index.html`, `main/index_html.h` | Self-contained page (no CDN), JSON API, live status, jog/zero, files, console, settings, firmware page, setup + calibration wizards. Replaces the stock ESP3D page. |
| Heightmap | `main/heightmap.c/.h`, `main/heightmap_plugin.c` | N×N probed grid (`M102` probe, `M103` report, `M104` clear), bilinear compensation, fail-closed checks. |
| A/B OTA | `main/ota.c`, `main/ota.h` | Upload to inactive slot, image validation, health gate + rollback, boot-loop counter, `$RB`/`$RS`, BOOT-button rollback, per-slot status. |
| Machine setup | `main/ui.c` API + UI | Steps/mm, rates, acceleration, travel, limit polarity, direction invert, homing config, limit/probe/steps tests, settings backup/restore. |
| Build | `main/my_machine.h`, `platformio.ini`, `sdkconfig.dlc32_max`, `partitions_s3_8m_ota.csv`, `scripts/build_stamp.py` | `env:dlc32-max`, 8 MB A/B partition layout, real build stamp in the UI. |
| Docs | `docs/` | design notes, bring-up log, screenshots (`docs/screens/` to be added after flow validation). |

## Required submodule patches (not upstreamed yet)

Two changes live inside submodules and are kept as patches until they land upstream:

- `main/grbl` (`grblHAL/core`) — `N_AXIS 4` (X/Y/Z/A).
- `main/networking` (`grblHAL/Plugin_networking`) — do not mount the SD card
  from `ftpd_init()` (race with the startup mount can crash the board).

Apply them after `git submodule update`:

```sh
python3 scripts/patch_submodules.py
# or: git -C main/grbl apply ../../patches/grbl-naxis4.patch
#     git -C main/networking apply ../../patches/networking-ftpd-sd-mount.patch
```

See `docs/LOCAL_PATCHES.md` for details.

## Build, flash, update

```sh
git submodule update --init --recursive
python3 scripts/patch_submodules.py
pio run -e dlc32-max
# app image: .pio/build/dlc32-max/dlc32-max.bin
```

- **OTA (recommended):** UI → Settings → Firmware, or
  `curl -H "X-OTA-Password: <pw>" --data-binary @dlc32-max.bin http://<ip>:8080/ota`
- **USB recovery:** `esptool.py --chip esp32s3 -p /dev/ttyUSB0 write_flash 0x10000 dlc32-max.bin`
  (full recovery image at `0x0`).
- **Credentials are not in this repo:** OTA password from
  `main/ota_secret.h` (see `scripts/make_ota_secret.py`, `docs/SECRETS.md`);
  WiFi via `$74/$75` or `tools/set_wifi.py`.
- **A/B layout:** firmware wrote into the inactive slot always leaves the
  previous build as fallback; keep the other slot on a known-good image.

### Install as a web app (installable UI / PWA)

Chrome/Edge can install the UI as a standalone window ("app"), but a browser
only allows this on a **secure context**. Two things are required:

1. **HTTPS** (or localhost). The board currently serves plain HTTP, so install
   is blocked with "this app cannot be installed".
   - Long term: serve the UI over HTTPS (`esp_https_server`, port 443) with a
     self-signed certificate that is trusted on the device.
   - For testing on desktop, the flag workaround works in both Chromium
     browsers:
     - Chrome: `chrome://flags/#unsafely-treat-insecure-origin-as-secure`
     - Edge: `edge://flags/#unsafely-treat-insecure-origin-as-secure`
     add `http://<board-ip>`, enable, then relaunch the browser.
     (Also available in Chrome for Android; not available on iOS Safari.)
2. **Web app manifest + icons**: `name`, `short_name`, `start_url`,
   `display: standalone`, `theme_color`, and 192x192 + 512x512 maskable icons
   (plus a service worker for reliable install behaviour).

On iOS Safari there is no PWA install prompt for HTTP origins; use
"Add to Home Screen" (browser shortcut, opens in a tab).

Without HTTPS the UI still works fully in a normal browser tab; only the
standalone-app installation needs the secure context.

## Validation status

- Calibrated: X/Y/Z steps/mm, max rates, travel, homing, limit switches,
  hard limits.
- Pending: wizard job flows (PCB/flatten/run), SD job streaming (P2),
  heightmap import/export, final screenshot set in `docs/screens/`.

## ESP32 grblHAL driver

__Important:__  
If enabling ftp upload to the SD card then _ffconf.h_ in the ESP SDK has to be edited, `#define FF_FS_RPATH` must be changed to 2 or you will get a compiler error.  
_ffconf.h_ is located in the subfolder _esp-idf\components\fatfs\src_ in the ESP32 SDK installation. The ESP32 SDK is typically installed in the user folder.

This driver can be built with the [Web Builder](http://svn.io-engineering.com:8080/?driver?driver=ESP32).

### How to build using ESP-IDF v4.3:

While this manual briefly describes basic build process on Linux OS, you can find more details
as well as differences for building on other OS at this webpage:

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#step-2-get-esp-idf

First you have to prepare esp-idf v4.3:

```bash
#Create directory and clone esp-idf into it:
mkdir -p ~/esp
cd ~/esp
git clone -b release/v4.3 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git

#Prepare build environment and toolchain:
cd ~/esp/esp-idf
./install.sh
. ~/esp/esp-idf/export.sh
```

Then get the grblHAL driver code:

```bash
#Create directory and clone the ESP32 grblHAL driver into it:
git clone --recursive https://github.com/grblHAL/ESP32.git
```

Go into the `ESP32/main` directory and modify settings in `grbl/config.h` and `CMakeLists.txt` as needed.
Pin assignments and board specific config is in `*_map.h` files for each individual board.

Run `idf.py build` from the `ESP32` directory.
This will build the firmware image which can be later flashed into ESP32 device.

Note that `idf.py` command is only available in terminal window which was previously configured
using the `. ~/esp/esp-idf/export.sh` command.

After build is completed you will be instructed on how to flash firmware into the device.
Typically you can use command similar to this: `idf.py -p /dev/ttyUSB0 flash`

Once flashing is complete, your CNC controller is ready to be configured and used.


### Using Docker

If you're familiar with [Docker](https://docker.io), you can use it to build grblHAL in a self-contained environment without installing the complete toolchain on your system:

- prepare and configure the codebase as described above
- build with `docker run -it --rm -v $(pwd):/grbl -w /grbl espressif/idf:release-v4.3 idf.py build`
- flash with `docker run -it --rm -v $(pwd):/grbl --privileged -v /dev:/dev -w /grbl/drivers/ESP32 espressif/idf:release-v4.3 idf.py -p /dev/ttyUSB0 flash`

### Building with user defined plugin

The file containing `my_plugin_init()` has to be added to _CMakeLists.txt_ in the [SRCS list](https://github.com/grblHAL/ESP32/blob/38dde1140d885fc847a0fa9c643cddd3fb1d02f4/main/CMakeLists.txt#L162)
and [grbl/my_plugin.c](https://github.com/grblHAL/ESP32/blob/38dde1140d885fc847a0fa9c643cddd3fb1d02f4/main/CMakeLists.txt#L178) has to be removed from it to be linked correctly.

### Changelog/Notes:

---

__NOTE:__ _grbl/config.h_ or _CMakeLists.txt_ may need modification before compilation. If needed an `#error` (with instructions) will be generated when compiling.


__NOTE:__ Configuration has been simplified a bit, primarily change options in [`CMakeLists.txt`](https://github.com/grblHAL/ESP32/blob/master/main/CMakeLists.txt) to enable/disable.
Configuration of options in _my_machine.h_ is turned off in _CMakeLists.txt_ by default due to this.


---

### Credits:

dns_server.c is Copyright (c) 2019 Tony Pottier - from his [ESP32 WiFi Manager](https://github.com/tonyp7/esp32-wifi-manager) 

Snippets of code is extracted from Espressif ESP-IDF examples which are public domain.

---
2023-09-20

