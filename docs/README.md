# Screenshots & design docs

Screens of the DLC32 MAX web UI, ordered by the setup flow.

All images are in this folder; they are captured from the live board.


## 1. Initial setup (first-time wizard)

### First-time setup, step 1: limit switches with wiring diagram and the embedded limits card.

![First-time setup, step 1: limit switches with wiring diagram and the embedded limits card.](webui-p2-setup-wizard.png)

### Limit switch step: Live/Seen/Invert controls, presets, hard/soft/homing flags.

![Limit switch step: Live/Seen/Invert controls, presets, hard/soft/homing flags.](webui-p2-setup-limits.png)

### No limit switches: Skip cascades to 'homing unavailable' and manual travel.

![No limit switches: Skip cascades to 'homing unavailable' and manual travel.](webui-p2-setup-noswitch.png)

### Info popup explaining what is lost without switches.

![Info popup explaining what is lost without switches.](webui-p2-setup-noswitch-help.png)

### Last steps of the wizard (limits -> manual travel).

![Last steps of the wizard (limits -> manual travel).](webui-p2-setup-wizard-last.png)

### Wizard landing page.

![Wizard landing page.](webui-p2-setup.png)


## 2. Calibration wizard

### Ordered calibration: Home -> X/Y/Z steps -> feed/accel -> limits -> travel.

![Ordered calibration: Home -> X/Y/Z steps -> feed/accel -> limits -> travel.](webui-p2-calibration.png)

### Final step: travel auto-filled from the DRO.

![Final step: travel auto-filled from the DRO.](webui-p2-calibration-travel.png)

### Direction invert with per-axis Test +1 mm buttons.

![Direction invert with per-axis Test +1 mm buttons.](webui-p1-dir-test.png)

### Homing config: direction ($23), pull-off, seek/approach rates, last alarm.

![Homing config: direction ($23), pull-off, seek/approach rates, last alarm.](webui-p1-homing-card.png)

### Homing rates/pull-off fields.

![Homing rates/pull-off fields.](webui-p1-homing-rates.png)

### Latched last alarm (e.g. ALARM:9 homing fail).

![Latched last alarm (e.g. ALARM:9 homing fail).](webui-p1-last-alarm.png)

### Merged limit-switch card with the motion safety flags.

![Merged limit-switch card with the motion safety flags.](webui-p1-limits-merged.png)

### Steps/backlash test result.

![Steps/backlash test result.](webui-p1-machine-cal.png)


## 3. Main page

### Main page with live DRO and tiles (Move, Wizards, Files, Job, Console, Settings).

![Main page with live DRO and tiles (Move, Wizards, Files, Job, Console, Settings).](webui-p1-main.png)

### Desktop view.

![Desktop view.](webui-p1-desktop.png)

### Phone view.

![Phone view.](webui-p1-mobile.png)

### Navigation after breadcrumbs were removed (stack-based Back).

![Navigation after breadcrumbs were removed (stack-based Back).](webui-p2-nocrumbs.png)


## 4. Move

### Move page: actions, XY pad, Z column, feeds.

![Move page: actions, XY pad, Z column, feeds.](webui-p1-move.png)

### Move page on a phone.

![Move page on a phone.](webui-p1-move-mobile.png)

### XY/Z feed fields.

![XY/Z feed fields.](webui-p1-move-feeds.png)

### Motion buttons disabled while the machine is not idle.

![Motion buttons disabled while the machine is not idle.](webui-p1-move-disabled.png)

### Back stack returning to the wizard after Move.

![Back stack returning to the wizard after Move.](webui-p1-back-stack.png)


## 5. Wizards

### PCB wizard step 1: project folder selection.

![PCB wizard step 1: project folder selection.](webui-p1-wizard-step1.png)

### PCB wizard step 2: setup.

![PCB wizard step 2: setup.](webui-p1-wizard-step2.png)

### PCB wizard step 4: heightmap (Continue/Skip/Cancel).

![PCB wizard step 4: heightmap (Continue/Skip/Cancel).](webui-p1-wizard-step4.png)

### PCB wizard step 5: run sequence.

![PCB wizard step 5: run sequence.](webui-p1-wizard-step5.png)

### Wizard dialog design: Continue / Skip / Cancel.

![Wizard dialog design: Continue / Skip / Cancel.](webui-wizard-dialogs.png)

### Earlier PCB wizard capture.

![Earlier PCB wizard capture.](webui-p1-wizard-pcb.png)

### Heightmap opened from the wizard context.

![Heightmap opened from the wizard context.](webui-p1-heightmap-from-wizard.png)

### Wizard on a phone.

![Wizard on a phone.](webui-p1-mobile-wizard.png)


## 6. Heightmap

### Probe button disabled with spinner while probing.

![Probe button disabled with spinner while probing.](webui-p1-probing-busy.png)

### Heightmap page (older breadcrumb version).

![Heightmap page (older breadcrumb version).](webui-p1-crumb-heightmap.png)


## 7. Files / Console

### Files page with the SD card mounted.

![Files page with the SD card mounted.](webui-p1-files.png)

### Console with controller output.

![Console with controller output.](webui-p1-console.png)


## 8. Settings

### Settings as an icon grid: setup, change settings, view, firmware, calibration, diagnostics, backup.

![Settings as an icon grid: setup, change settings, view, firmware, calibration, diagnostics, backup.](webui-p2-settings.png)

### All settings view + $$ dump.

![All settings view + $$ dump.](webui-p2-settings-view.png)

### Firmware & About: slots, build, upload, rollback.

![Firmware & About: slots, build, upload, rollback.](webui-p2-firmware.png)

### About section with build date/time and branch.

![About section with build date/time and branch.](webui-p1-about.png)


## 9. Machine setup / parameters

### Machine setup grid.

![Machine setup grid.](webui-p2-machine-menu.png)

### Parameters + limit/probe/steps cards.

![Parameters + limit/probe/steps cards.](webui-p1-machine.png)

### Editable parameters with save.

![Editable parameters with save.](webui-p1-machine-edit.png)

### Help popup for machine parameters.

![Help popup for machine parameters.](webui-p1-machine-help.png)

### Limit polarity and motion flags.

![Limit polarity and motion flags.](webui-p1-polarity.png)

### Latched 'seen' badges for limit switches.

![Latched 'seen' badges for limit switches.](webui-p1-limits-seen.png)

### Main-page reminder when steps/travel are still default.

![Main-page reminder when steps/travel are still default.](webui-p1-setup-banner.png)

### Shared card components used by both Machine and the wizard.

![Shared card components used by both Machine and the wizard.](webui-p2-shared-cards.png)


## 10. Job

### Job page (streaming arrives in P2).

![Job page (streaming arrives in P2).](webui-p2-job.png)


## 11. Hardware

### MKS DLC32 MAX limit connector pin map.

![MKS DLC32 MAX limit connector pin map.](dlc32max-limit-connectors.png)


## 12. Design iterations

### Stock ESP3D page (before).

![Stock ESP3D page (before).](webui-current.png)

### Design v1.

![Design v1.](webui-design.png)

### Design v2: 3D mesh + matrix.

![Design v2: 3D mesh + matrix.](webui-design-v2.png)

### Design v3: heightmap import, tabs.

![Design v3: heightmap import, tabs.](webui-design-v3.png)

### Design v4: heightmap page, settings.

![Design v4: heightmap page, settings.](webui-design-v4.png)

### Design v5: legacy wizard icons.

![Design v5: legacy wizard icons.](webui-design-v5.png)

### Design v6: final IA.

![Design v6: final IA.](webui-design-v6.png)

### Navigation study.

![Navigation study.](webui-nav-design.png)


## Other captures

### webui-p1-back-restored.png

![webui-p1-back-restored.png](webui-p1-back-restored.png)

### webui-p1-back-test.png

![webui-p1-back-test.png](webui-p1-back-test.png)

### webui-p1-firmware.png

![webui-p1-firmware.png](webui-p1-firmware.png)

### webui-p1-help-hard.png

![webui-p1-help-hard.png](webui-p1-help-hard.png)

### webui-p1-machine-dirs.png

![webui-p1-machine-dirs.png](webui-p1-machine-dirs.png)
