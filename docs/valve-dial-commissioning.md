# Valve dial commissioning

## Source and host gate

The source artifact compiled and host tested for Task 4 is firmware commit `8d7f4b112f1f2510cdcb226293f151d69e81fa48` on `codex/roon-valve-dial`. ESP-IDF 5.5.5 built the ESP32-S3 image `idf_app/build/hiphi_dial.bin` at size `0x1fb0e0` (smallest app partition `0x280000`) with SHA-256 `6d2a8c9fdb3ccd81ffca3da43101b78c8f66eededcd554664c12cd6990e41d71`. The Pi source is `5ae3c3ea53feae37147b72782308770605113fde`; the bridge compose example is `2c119689e22e988f343226d49c0b2dc195707437`. This documentation and integration-test follow-up commit is later than the compiled firmware source commit. Its source changes are limited to tests and documentation. The image is unflashed; physical verification remains open.

For a future build, record the source commit, binary hash, and ESP-IDF version together. The ignored `.superpowers/sdd/2026-09-29-waveshare-dial-roon-valves/task-4-report.md` records this run's commands and results. A firmware source edit or rebuild needs a new artifact record.

Run `PATH=/tmp/knob-bin:$PATH ./scripts/test_valve_dial.sh`, `bash /tmp/knob-idf-build.sh`, and `git diff --check`. A green host suite and ESP32-S3 build establish source and compilation evidence only; they do not establish flashed behavior or valve safety. The host integration session links the real portable valve client, UI, logic, and Roon bridge command planner to fake Pi and bridge responses; it does not exercise ESP HTTP transport, Rust bridge runtime, or physical hardware.

The custom firmware has no bridge OTA check or update action. Configure the bridge deployment with `FIRMWARE_AUTO_UPDATE=false` so it does not auto-download upstream roon-knob images. Roon control continues through the existing bridge; valve requests go directly to the Pi. Future remote firmware updates require a dedicated controlled image feed, signature verification, rollback design, and a separate review and physical test.

## First physical session

1. Keep the **24 V valve supply disconnected**. Record the exact firmware SHA and binary hash, flash that binary over USB, and observe a sustained boot with version and Wi-Fi logs that contain no Pi token.
2. Provision the Pi URL and a newly rotated display token only on this Waveshare dial. Use the local `/valves-config` form; never place the token in Git, a URL query string, screenshots, or logs. Confirm the admin and Node-RED credentials remain separate from the display token. Retire the CrowPanel copy of the old display token and disable its access.
3. With 24 V still disconnected, verify Roon now-playing, transport and volume through the bridge, valve page navigation by horizontal swipe, vertical art gestures in their intended context, and the uninterrupted 2-second SUPPLY hold. Verify an immediate DRAIN action, a single 600-second Pi interval, countdown correction from Pi status, and status recovery after Wi-Fi loss. Confirm an ambiguous or queued POST is not replayed after reconnection. Use a mock Pi/bridge first; physical network observations must be recorded separately.
4. Read back both GPIO output pins in DRAIN and SUPPLY with 24 V disconnected, and verify the Pi's fail-safe and lease behavior. GPIO readback alone does not prove relay or valve position. Record both-pin observations against the exact Pi and firmware SHAs.

Connecting 24 V and exercising live valves is a separate explicit approval gate after the disconnected checks. No build, host test, or mock session is evidence that a physical relay or valve moved.
