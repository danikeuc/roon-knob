# Valve dial commissioning

## Source and host gate

The Pi manual timed controller source was verified at commit `5ae3c3ea53feae37147b72782308770605113fde`. The firmware artifact must be identified by the final commit from `git rev-parse HEAD` on branch `codex/roon-valve-dial`, plus the binary hash and ESP-IDF 5.5.5 build log. The ignored `.superpowers/sdd/2026-09-29-waveshare-dial-roon-valves/task-4-report.md` records the final firmware SHA from this implementation. A later edit or rebuild creates a different artifact and needs a new record.

Run `PATH=/tmp/knob-bin:$PATH ./scripts/test_valve_dial.sh`, `bash /tmp/knob-idf-build.sh`, and `git diff --check`. A green host suite and ESP32-S3 build establish source and compilation evidence only; they do not establish flashed behavior or valve safety. The PC simulator does not exercise this Dial specific Pi integration.

The custom firmware has no bridge OTA check or update action. Configure the bridge deployment with `FIRMWARE_AUTO_UPDATE=false` so it does not auto-download upstream roon-knob images. Roon control continues through the existing bridge; valve requests go directly to the Pi. Future remote firmware updates require a dedicated controlled image feed, signature verification, rollback design, and a separate review and physical test.

## First physical session

1. Keep the **24 V valve supply disconnected**. Record the exact firmware SHA and binary hash, flash that binary over USB, and observe a sustained boot with version and Wi-Fi logs that contain no Pi token.
2. Provision the Pi URL and a newly rotated display token only on this Waveshare dial. Use the local `/valves-config` form; never place the token in Git, a URL query string, screenshots, or logs. Confirm the admin and Node-RED credentials remain separate from the display token. Retire the CrowPanel copy of the old display token and disable its access.
3. With 24 V still disconnected, verify Roon now-playing, transport and volume through the bridge, valve page navigation by horizontal swipe, vertical art gestures in their intended context, and the uninterrupted 2-second SUPPLY hold. Verify an immediate DRAIN action, a single 600-second Pi interval, countdown correction from Pi status, and status recovery after Wi-Fi loss. Confirm an ambiguous or queued POST is not replayed after reconnection. Use a mock Pi/bridge first; physical network observations must be recorded separately.
4. Read back both GPIO output pins in DRAIN and SUPPLY with 24 V disconnected, and verify the Pi's fail-safe and lease behavior. GPIO readback alone does not prove relay or valve position. Record both-pin observations against the exact Pi and firmware SHAs.

Connecting 24 V and exercising live valves is a separate explicit approval gate after the disconnected checks. No build, host test, or mock session is evidence that a physical relay or valve moved.
