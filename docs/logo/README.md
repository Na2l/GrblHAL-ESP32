# Logo / app icon

One mark, four uses: app icon (PWA/manifest), favicon, README banner and
splash. Concept: an isometric **X/Y/Z trihedron** with a glowing **V-bit**
at the origin, on a dark tile with PCB traces — "3018-Pro cuts PCBs".

| File | Use |
|---|---|
| `icon-master.svg` | source (1024 canvas, squircle) |
| `icon-1024.png` / `icon-512.png` / `icon-192.png` | PWA manifest icons (any) |
| `icon-maskable-512.png` | PWA maskable icon (80 % safe zone) |
| `apple-touch-180.png` → `icon-180.png` | iOS home screen |
| `icon-48.png` / `icon-32.png` | favicon |
| `logo-cnc-640.png` / `logo-cnc-320.png` | logo lockup for README/docs (icon + "CNC") |
| `lockup-cnc.svg` | lockup source (icon + "CNC") |

Brand colors: background `#0a0f17` / `#121b2b`, accent blue `#4f9cf9` →
`#9fd4ff`, highlight `#ffffff`. Keep the bit and axes inside the central
80 % for maskable crops.
