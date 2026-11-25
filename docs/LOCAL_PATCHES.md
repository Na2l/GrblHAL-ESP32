# Local patches (not upstream)

The DLC32 MAX build needs two small patches inside the submodules. They are not
committed to the submodule repos; re-apply them after a submodule update with:

```sh
python3 scripts/patch_submodules.py
```

## 1. `main/grbl/config.h` — 4 axes

```diff
-#define N_AXIS 3 // Number of axes
+#define N_AXIS 4 // Number of axes (MKS DLC32 MAX: X, Y, Z + A)
```

The MKS DLC32 MAX exposes X/Y/Z/A, so the core must compile with 4 axes.
(Setting this in `my_machine.h` alone is not enough: the core TUs do not see it.)

## 2. `main/networking/ftpd.c` — do not mount SD from `ftpd_init()`

```diff
 #if FS_ENABLE & FS_SDCARD
-        sdcard_getfs(); // try to mount SD card
+        // Do NOT mount the SD card here: ftpd_init() can be called from the
+        // WiFi/IP event context while the SD startup mount is still pending,
+        // and a concurrent f_mount()/diskio registration crashes.
+        // The SD card is mounted by sdcard_auto_mount() at startup, by $FM,
+        // or when the card is detected; FTP picks mounts up via vfs.on_mount.
 #endif
```

`ftpd_init()` runs from the WiFi/IP event context. With an SD card inserted it
called `sdcard_getfs()` → `sdcard_mount()` → `f_mount()` → `ff_diskio_register()`
concurrently with the startup mount, which crashed (LoadProhibited) and put the
board into a boot loop — the A/B rollback then reverted the new firmware.

## Patch files

Ready-to-apply diffs are kept in `patches/`:

```sh
git -C main/grbl apply ../../patches/grbl-naxis4.patch
git -C main/networking apply ../../patches/networking-ftpd-sd-mount.patch
```

or simply run `python3 scripts/patch_submodules.py` (idempotent).

Both submodule changes are also being prepared as forks + pull requests to the
upstream grblHAL repos.
