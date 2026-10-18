# Light-page swipe responsiveness — 2026-10-18

Prepared, not flashed. Active countdown called lv_line_set_points every UI iteration. The installed LVGL implementation always refreshes size and invalidates the line; the ring spans nearly the full screen. The regression test failed on repeated identical geometry before the change and passed after caching the displayed point count. Geometry changes still update immediately; ON/OFF, mode, timing and stale-state logic are unchanged.

Host tests verify both navigation directions with active light and no actuator requests, plus actual touch callback horizontal gestures at 0/90/180/270 degrees. Light, zone-swipe suites and ESP-IDF ESP32-S3 build pass. Hardware symptom correlation remains unverified; this is a candidate responsiveness fix, not a confirmed physical repair.

Firmware SHA256: `6a22b6ed61838c85a2bf253de458387c609b65424d8bf64cb4b8ff0d4ed96332`. Prior unrelated worktree edits retained. No firmware upload, GPIO command, Pi modification or release performed.

## Follow-up: complete light-page input blockage

Owner clarified that all controls become unresponsive after entering the light page, irrespective of light output. The ring redraw optimization is not accepted as a demonstrated cause of this wider failure. Read-only COM3 sampling received serial data, but current firmware provides no raw input/gesture evidence. Diagnostic candidate adds bounded non-secret input polling/guard counters, per-release gesture classification and deferred page-switch logging. Actual production touch/zone/playlist tests and ESP32-S3 build pass. No upload or physical action performed. Diagnostic firmware SHA256: `d87c151ad3f1005a2ca0f41bc506e8c02b546e105ce1190542a6825aa023eba2`. Hardware disconnection confirmation pending before upload.

## Diagnostic flash verified

Owner confirmed both loads disconnected. Restricted SSH before flashing showed shower MANUAL_DRAIN/user_off and light MANUAL_OFF/OK with desired_on=false and gpio_on=false. USB chip identity matched ESP32-S3 MAC d0:cf:13:1e:15:44. Prior app partition was backed up and verified; NVS read twice before update and byte-compared after. Only the app at 0x10000 was written, readback verification passed, then the device was reset. Diagnostic image d87c151ad3f1005a2ca0f41bc506e8c02b546e105ce1190542a6825aa023eba2 is flashed. Non-secret touch capture started; physical swipe outcome remains pending. No ON/SUPPLY command issued.

## Captured input and render correction

The owner completed the diagnostic swipe sequence. Capture recorded four exits from the light page (two toward Roon, two toward shower). No input/modal guard was active and no panic/reboot was observed. Steady light-page samples contained 24-30 touch callback reads per approximately five seconds; shorter attempts sometimes had zero measured movement. This disproves a permanent input lock in this capture and identifies low input sampling as a plausible cause of missed swipes.

Production render() still called label and style setters on every UI tick. The installed LVGL label implementation reallocates/invalidates even identical text. A regression assertion that 40 unchanged ticks issue zero label updates failed before the correction. Rendering now compares an explicit visual-state key, including freshness, confirmed GPIO/health, manual/cooldown reason, countdown seconds, settings, visibility, options and pending state. Only changed display inputs reach LVGL setters. Freshness and network processing continue every tick; gesture thresholds and actuator policy are unchanged.

Light suite, production touch callback tests at four rotations, zone and playlist gesture tests, and git diff --check pass. Tests additionally verify same-second ON/OFF, FAULT, menu and settings updates, countdown progression and stale output handling. ESP-IDF ESP32-S3 build passes. Candidate SHA256: b255df14ac77a3bcab7a08f1e9fbb882338a145b8d34df067aa7282dfc3a0fd9. First backup attempt encountered a corrupt USB read packet before any flash write; retry uses 115200 baud. Physical responsiveness comparison is still pending.

## Render fix uploaded

Both large USB read attempts failed before writing. The previously saved diagnostic image was SHA256-checked and verified against the running app flash instead, then saved as the rollback app. NVS was read twice and compared before writing. App-only upload of b255df14ac77a3bcab7a08f1e9fbb882338a145b8d34df067aa7282dfc3a0fd9 and flash verification passed; post-upload NVS was byte-identical. Device identity remained d0:cf:13:1e:15:44. Backup: private/light-admin-20261015-f8391fb2/render-fix-rjdsce8b. Device reset succeeded. Post-fix physical swipe acceptance remains pending. No actuator commands issued.

## Owner acceptance

Owner confirmed "deluje smooth" after the render fix upload. Physical navigation responsiveness is accepted by the owner. Diagnostic capture stop requested; no actuator commands issued.
