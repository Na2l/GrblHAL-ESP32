#!/usr/bin/env python3
"""Apply the local grblHAL submodule patches needed by the DLC32 MAX build.

Run after `git submodule update --init --recursive` (and after updating the
submodules) so the build keeps working:

    python3 scripts/patch_submodules.py

Patches:
1. main/grbl/config.h        -> N_AXIS 4 (the driver map exposes X/Y/Z/A)
2. main/networking/ftpd.c    -> do not mount the SD card from ftpd_init()
                                (race with the startup mount crashes the board)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def patch(path, old, new, label):
    full = os.path.join(ROOT, path)
    try:
        with open(full, "r", encoding="utf-8", errors="surrogateescape") as fh:
            src = fh.read()
    except OSError as exc:
        print(f"  SKIP {label}: {exc}")
        return False
    if new in src:
        print(f"  OK   {label} (already applied)")
        return True
    if old not in src:
        print(f"  FAIL {label}: expected text not found in {path}")
        return False
    with open(full, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(src.replace(old, new, 1))
    print(f"  PATCHED {label}")
    return True


def main():
    ok = True
    print("Patching grblHAL submodules:")
    ok &= patch(
        "main/grbl/config.h",
        "#ifndef N_AXIS\n#define N_AXIS 3 // Number of axes\n#endif",
        "#ifndef N_AXIS\n#define N_AXIS 4 // Number of axes (MKS DLC32 MAX: X, Y, Z + A)\n#endif",
        "main/grbl config.h N_AXIS=4",
    )
    ok &= patch(
        "main/networking/ftpd.c",
        '#if FS_ENABLE & FS_SDCARD\n        sdcard_getfs(); // try to mount SD card\n#endif',
        '#if FS_ENABLE & FS_SDCARD\n'
        '        // Do NOT mount the SD card here: ftpd_init() can be called from the\n'
        '        // WiFi/IP event context while the SD startup mount is still pending,\n'
        '        // and a concurrent f_mount()/diskio registration crashes.\n'
        '        // The SD card is mounted by sdcard_auto_mount() at startup, by $FM,\n'
        '        // or when the card is detected; FTP picks mounts up via vfs.on_mount.\n'
        '#endif',
        "main/networking ftpd.c SD mount race",
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
