# Valve dial commissioning

## Source and host gate

The firmware source compiled and host tested after final review was commit `1bd04441a236e0693fa54bd1bd0a7437826f07ff` on `codex/roon-valve-dial`. ESP-IDF 5.5.5 built the ESP32-S3 image `idf_app/build/hiphi_dial.bin` at size `0x1fb240` (smallest app partition `0x280000`, free `0x84dc0`, 21%) with SHA-256 `edef9e7f5ff527dfdfff9164ef619f3ddefd9632d21b9a6a47fbade2e07871e1`. The Pi source is `5ae3c3ea53feae37147b72782308770605113fde`; the bridge compose example is `2c119689e22e988f343226d49c0b2dc195707437`. This evidence documentation commit follows the compiled firmware source commit. Hardware commissioning later found that this image could overflow the `valve_http` task stack, so it is superseded by the corrected artifact recorded below.

Final review regressions cover a continuous SUPPLY contact across timeout/recovery and reconnect, configuration changes during a hold or queued/in-flight requests, failed saves and clears, and a horizontal gesture while factory-reset confirmation is open. Saves and clears invalidate observations before storage changes, including failed saves. SUPPLY requires a fresh normal DRAIN observation for the current configuration. A new deliberate DRAIN remains available with validated current credentials even before a normal observation, including fault, malformed, or timed-out status; old contacts and old configuration work remain rejected. Requests retain one credential snapshot, and old configuration completions are discarded. An HTTP action already in flight may have reached the original Pi; changing settings cannot undo that action. The modal regression compiles the production visibility predicate and gesture classifier with simulated panel/dialog state; it does not drive physical LVGL input.

For a future build, record the source commit, binary hash, and ESP-IDF version together. The ignored `.superpowers/sdd/2026-09-29-waveshare-dial-roon-valves/final-fix-report.md` records this run's commands and results. A firmware source edit or rebuild needs a new artifact record.

Run `PATH=/tmp/knob-bin:$PATH ./scripts/test_valve_dial.sh`, `bash /tmp/knob-idf-build.sh`, and `git diff --check`. A green host suite and ESP32-S3 build establish source and compilation evidence only; they do not establish flashed behavior or valve safety. The host integration session links the production valve client, UI, logic, input resolver, action router, bridge client, and command planner. A fake Pi transport supplies status and action responses; a fake platform HTTP transport accepts Roon control JSON. The test classifies the horizontal gesture and calls the same deferred page-switch function used by the ESP display task before it routes Roon play/pause and next. The physical encoder path is also entered; volume is correctly refused because the fake bridge has no operational volume state. The session does not exercise the ESP touch/HTTP drivers, Rust bridge runtime, live Roon zone, or physical hardware.

The custom firmware has no bridge OTA check or update action. Configure the bridge deployment with `FIRMWARE_AUTO_UPDATE=false` so it does not auto-download upstream roon-knob images. Roon control continues through the existing bridge; valve requests go directly to the Pi. Future remote firmware updates require a dedicated controlled image feed, signature verification, rollback design, and a separate review and physical test.

## Shower-symbol UI candidate: 2026-09-30

Commit `3683b9bbbe3c77855a874e7f07b981e6851046d2` on `codex/valve-shower-ui` replaces the technical two-button valve page with the approved shower presentation. DRAIN shows the rain-shower head without water dots, a separate snowflake, outlined paired-relay status, `OFF`, and a 2-second `10 MIN` action. SUPPLY shows the water-dot grid, hides the snowflake, fills the paired-relay status, shows `ON`, and puts the Pi-derived remaining time in the action button. Unknown or stale state shows `FAULT` and `Status unavailable`; the sole available action remains a deliberate DRAIN. The UI does not claim physical relay or valve position.

The full valve host suite passed with a temporary Zig 0.13.0 C compiler after first failing on the old `DRAIN (0)` presentation. The sequence and mock integration tests cover the DRAIN/SUPPLY water-dot and snowflake visibility, 2-second SUPPLY hold, immediate DRAIN, 600-second server interval, Pi countdown correction, unknown-state DRAIN-only behavior, reconnect, stale responses, configuration changes, Roon routing, and token redaction. `git diff --check` also passed. ESP-IDF 5.5.5 built source commit `d63e6f71ff304e0d95b2439c441dd2d13c5f3d99`; `idf_app/build/hiphi_dial.bin` is `0x1fb920` bytes with SHA-256 `50f7b5163eefe733c98d22d997249d0ba2e280ec84394fd6f7f066a65e8e121b`, leaving `0x846e0` bytes free (21%) in the smallest application partition. The artifact has not been flashed, so display geometry, colors, touch feel, boot stability, and hardware behavior remain `NOT_VERIFIED` for this candidate.

## First physical session

1. Keep the **24 V valve supply disconnected**. Record the exact firmware SHA and binary hash, flash that binary over USB, and observe a sustained boot with version and Wi-Fi logs that contain no Pi token.
2. Provision the Pi URL and a newly rotated display token only on this Waveshare dial. Use the local `/valves-config` form; never place the token in Git, a URL query string, screenshots, or logs. Confirm the admin and Node-RED credentials remain separate from the display token. Retire the CrowPanel copy of the old display token and disable its access.
3. With 24 V still disconnected, verify Roon now-playing, transport and volume through the bridge, valve page navigation by horizontal swipe, vertical art gestures in their intended context, and the uninterrupted 2-second SUPPLY hold. Verify an immediate DRAIN action, a single 600-second Pi interval, countdown correction from Pi status, and status recovery after Wi-Fi loss. Confirm an ambiguous or queued POST is not replayed after reconnection. Use a mock Pi/bridge first; physical network observations must be recorded separately.
4. Read back both GPIO output pins in DRAIN and SUPPLY with 24 V disconnected, and verify the Pi's fail-safe and lease behavior. GPIO readback alone does not prove relay or valve position. Record both-pin observations against the exact Pi and firmware SHAs.

Connecting 24 V and exercising live valves is a separate explicit approval gate after the disconnected checks. No build, host test, or mock session is evidence that a physical relay or valve moved.

## Hardware evidence: 2026-09-30

The first image above was flashed to Waveshare ESP32-S3 MAC `d0:cf:13:1e:15:44`. The dial retained its Wi-Fi and Roon configuration, discovered the bridge at `192.168.111.130:8088`, selected the Sauna zone, displayed artwork, and switched between the Roon and valve pages by horizontal swipe. The valve page reported `DRAIN (0)` and readiness for timed supply. No Pi display token appeared in the captured serial log.

With the 24 V valve supply disconnected, a deliberate two-second SUPPLY hold produced one 600-second Pi interval. Pi GPIO 26 and 20 both changed from high in DRAIN to low in SUPPLY, then both returned high immediately after DRAIN. This proves the disconnected software/GPIO path only. It does not prove relay contacts or physical valve movement. The deployed Pi repository head was `dab832f4dd98095db8e68d2189d7f159abc93183`, based on tested control source `5ae3c3ea53feae37147b72782308770605113fde`, with `FREEZE_PROTECT_CONTROL_MODE=manual_timed`.

A single physical encoder detent changed the live Sauna Roon volume from -35 dB to -34 dB. During the same commissioning session, serial output then reported `A stack overflow in task valve_http has been detected` and the controller rebooted. The 6144-byte worker stack held a 2048-byte response plus the HTTP client call chain and was insufficient on the target. That first flashed artifact is therefore rejected even though its individual Roon and GPIO observations succeeded.

Commit `3b526fad42a40f1c32b7c2d38509385b621c4cdd` raises the `valve_http` stack to 12 KiB, uses that named size at task creation, and records the stack high-water mark after requests. The regression gate first failed against the 6144-byte implementation and passed after the change. `PATH=/tmp/knob-bin:$PATH ./scripts/test_valve_dial.sh`, `bash /tmp/knob-idf-build.sh`, and `git diff --check` all passed. ESP-IDF 5.5.5 produced a `0x1fb2c0`-byte application with SHA-256 `66624aa234280d68ffc0192b73b42ed317ba963836bab5456d6dfecbc3fbb6ad`; the smallest application partition retained `0x84d40` bytes free (21%).

The corrected bootloader, partition table, initial OTA data, and application were flashed to the same MAC and read back with esptool 5.3.1; all four digests matched. Two clean hardware boots restored Wi-Fi, the Sauna zone, and artwork. During the second 50-second serial run, repeated valve status requests reduced the observed free-stack watermark from 6472 to 6280 and finally 6136 bytes, with no overflow, reset, or fault. A single physical encoder detent on the corrected image changed Sauna from -37 dB to -36 dB; bridge readback confirmed -36 dB and the dial HTTP server still returned 200 afterward. A passive serial capture then recorded the physical play-button callbacks while bridge readback confirmed the Sauna zone first in `playing` and finally back in `paused`, still at -36 dB. The dial continued to return HTTP 200 and did not reset. The 24 V valve supply remained disconnected throughout.

## Shower-symbol physical evidence: 2026-09-30

At `2026-09-30T23:15:13+02:00`, the shower-symbol candidate from branch head `116e14602bf5b3c6af8a3c23b863b3f349856a59` was tested on the same Waveshare ESP32-S3, MAC `d0:cf:13:1e:15:44`, with the 24 V valve supply disconnected. The flashed application `idf_app/build/hiphi_dial.bin` was `0x1fb920` bytes with SHA-256 `50f7b5163eefe733c98d22d997249d0ba2e280ec84394fd6f7f066a65e8e121b`. Esptool 4.12.0 wrote the bootloader, partition table, initial OTA data, and application; a separate `verify_flash` pass reported `digest matched` for all four segments.

A 60-second serial observation recorded ESP-IDF 5.5.5 boot, display and CST816 touch initialization, restored Wi-Fi configuration, IP `192.168.114.173`, the Sauna Roon zone, artwork retrieval, and `6476/12288` bytes free at the first reported `valve_http` stack high-water mark. No stack-overflow, panic, abort, brownout, or Guru Meditation marker appeared in 281 captured lines. The dial HTTP root returned 200 after boot. No captured line was removed by the token/password redaction filter.

The user physically observed the DRAIN page with the approved shower head without water dots, snowflake, paired-relay `OFF` indicator, and `HOLD 2s / 10 MIN` action. One uninterrupted two-second hold changed the display to SUPPLY with water dots, `ON`, and the countdown near ten minutes. One short press then returned the display immediately to DRAIN: the dots disappeared and the snowflake and `OFF` indication returned. This verifies the physical display/touch presentation and the authorized device-to-Pi action round trip for this disconnected test.

With 24 V still disconnected, the user then read both Raspberry Pi outputs with `pinctrl` across one complete transition:

| Observed at | Display/command state | GPIO26 | GPIO20 |
| --- | --- | --- | --- |
| `2026-09-30T21:19:33+00:00` | DRAIN / OFF | `hi` | `hi` |
| `2026-09-30T21:20:24+00:00` | SUPPLY / ON | `lo` | `lo` |
| `2026-09-30T21:21:05+00:00` | DRAIN / OFF | `hi` | `hi` |

This verifies the coupled software/GPIO sequence `DRAIN hi/hi -> SUPPLY lo/lo -> DRAIN hi/hi` for the exact dial candidate and the Pi configuration active during the observation. At `2026-09-30T21:24:42+00:00`, the user read back deployed Pi repository commit `dab832f4dd98095db8e68d2189d7f159abc93183`, `FREEZE_PROTECT_CONTROL_MODE=manual_timed`, and both `freeze-protect.service` and `freeze-protect-pair-gpio.service` as active. GPIO levels do not prove relay contacts, valve position, or water routing. At this disconnected stage, every 24 V, hydraulic, and energized fail-safe observation remained `NOT_VERIFIED`.

At `2026-09-30T23:29:07+02:00`, the user reported that the bounded 24 V functional test described for this candidate had already been performed and worked: SUPPLY enabled the hot- and cold-water feed, and the subsequent DRAIN closed the feeds and opened the drain path. This is user-observed physical functional evidence for the normal energized command path. The exact actuation timestamp, duration, independent measurement, and photo or video evidence were not separately recorded.

| Energized outcome | Evidence for this candidate |
| --- | --- |
| Normal SUPPLY then DRAIN | `VERIFIED` by user physical observation |
| Communication loss | `NOT_VERIFIED` with energized valves |
| Pi process crash or hang | `NOT_VERIFIED` with energized valves |
| Pi controller reset or reboot | `NOT_VERIFIED` with energized valves |
| Power loss and restoration | `NOT_VERIFIED` with energized valves |

The live functional observation does not establish those four fault outcomes, long-duration reliability, exact relay-contact timing, or independent valve-position feedback. DRAIN remains the required final state.

## PT100 shower display development candidate: 2026-10-01

GitHub issue [#7](https://github.com/danikeuc/roon-knob/issues/7) tracks the optional PT100 temperature display. The firmware branch `codex/waveshare-pt100-display` starts at the immutable `v2.5.3-valve.1` source commit `b32fd8a2ee1f3b2731fbc13179c605e86a8c0d0a`. The current candidate was built from source commit `385979069a20c2a9f11ab7d02ef656563cd7855e`, which recognizes JSON Unicode-escaped aliases of the optional telemetry keys and moves the shower-page temperature label inside the 360 × 360 round display's visible circle. This documentation commit follows that source commit and records its build evidence; it does not change the compiled source or claim that the binary was built from the later documentation head. The earlier candidate from `a8d90fad9cf3b01300bae516fcc2687fbae123eb` is superseded.

Before the fix, host regressions failed on an escaped optional key and on the round-display geometry assertion. From the clean fixed source commit, `PATH=/tmp/knob-bin:$PATH ./scripts/test_valve_dial.sh` passed the valve configuration, parser/client, mock Pi and Roon bridge integration, valve UI sequence, and settings gesture tests. The geometry test uses a 16 px advance per glyph, 24 px line height, and a 4 px circle margin; the bundled LVGL Montserrat 20 font descriptors have narrower advances for the representative temperature characters and a 22 px line height. `bash /tmp/knob-idf-build.sh` completed an ESP32-S3 build with `ESP-IDF v5.5.5` (`idf.py --version` in the build environment) and esptool.py v4.12.0. The resulting `idf_app/build/hiphi_dial.bin` is 2,102,064 bytes (`0x201330`) with SHA-256 `c2abd6d1168cac05d3eeb1581fd11d7c64c6e91a43644878584f148c8f4e4b00`. The smallest application partition is `0x280000` bytes, leaving `0x7ecd0` bytes free (20%). `git diff --check` passed. The build left the tracked tree clean; `idf_app/sdkconfig` is generated and ignored, and tracked `idf_app/sdkconfig.defaults` was unchanged from the release base.

These are source, host-test, and compilation results. At the source-only evidence point, this PT100 candidate had not been flashed or observed on the physical Waveshare dial; the later flash observation below supersedes that status. At that source-only stage, shower-page geometry and visibility, Wi-Fi behavior, PT100 accuracy and fault response, relay contacts, valve motion, and water routing were unverified. This source-only work did not connect the 24 V valve supply or perform a SUPPLY action; commissioning requires 24 V to remain disconnected. The hardware session below uses the exact artifact hash above and records the observed flash, boot and display evidence separately.

## PT100 candidate flash and bounded boot — 2026-10-01

The user confirmed 24 V valve power disconnected and approved continuing with COM3 without repeating a disconnect/reconnect check. Before flash, esptool 4.12.0 identified ESP32-S3 revision v0.2, MAC `d0:cf:13:1e:15:44`, matching the previously recorded dial.

Application source `385979069a20c2a9f11ab7d02ef656563cd7855e`, size 2,102,064 bytes, SHA-256 `c2abd6d1168cac05d3eeb1581fd11d7c64c6e91a43644878584f148c8f4e4b00` was flashed with the build-defined bootloader, partition table and initial OTA data. A separate `verify_flash` pass matched the digests of all four regions. NVS at 0x9000..0xcfff was outside the erased/written regions.

A 50-second serial observation from `2026-10-01T01:30:05.738586+00:00` to `2026-10-01T01:30:55.974698+00:00` examined 274 lines in memory and emitted only allowlisted metadata. It recorded one project startup, ESP-IDF v5.5.5, ELF hash prefix `0ac107ac3`, IP `192.168.114.173` and `5896/12288` bytes free at the valve HTTP task watermark. No selected panic, stack-overflow, assertion, brownout or task-watchdog markers were found. HTTP root returned 200 after observation. The inherited version string `2.5.3-valve.1` does not distinguish this candidate from the immutable release; source and binary hashes above identify the flashed candidate.

Operator-supplied Pi evidence records installed source `7a24387efbbe0772ae88646d1d98182feca43369` and authenticated display JSON reporting `24.212243310943986` degrees C / `HEALTHY`, `manual_timed`, `MANUAL_DRAIN`, zero remaining seconds and no timed deadline. After flash, restricted SSH status independently reported active services and GPIO26/GPIO20 output/high.

The candidate is now flashed and boot-observed. The operator subsequently reported `24,3` on the shower page and no temperature on the Roon page. This confirms the observed decimal-comma value and shower-only placement; no screenshot or exact observation timestamp was provided. The operator then reported `24,4` degrees C on an independent thermometer near PT100, approximately 0.1 degree C above the earlier dial value. This sequential one-point comparison is not calibration; reference identity, uncertainty and stabilization were not recorded. At this observation point, signed/extreme-value legibility, the degree glyph in isolation, repeated/cold-point comparisons, sensor-fault/stale telemetry fallback and Wi-Fi recovery had not been verified. The later operator decision below updates Wi-Fi and sensor-test status. The subsequent Roon check is recorded below. No SUPPLY action or energized valve test was performed in this session. PR #8 remains draft.

## Hub service interruption and recovery — operator observation

For the installed Pi source and flashed dial artifact recorded above, the operator was given a trusted-console sequence to stop `freeze-protect.service`, wait 20 seconds and restart it, with an EXIT trap to restore the service. With 24 V valve power disconnected and the shower page visible, the expected presentation was `---` with `FAULT / Status unavailable` during the interruption, followed by temperature and normal DRAIN presentation after restart.

The operator replied `ja vse je delalo tako kot mora` (yes, everything worked as expected), confirming those expected transitions. No console transcript or measured transition latency was supplied for this test; 20 seconds is the prescribed interruption, not an independently measured duration. A subsequent restricted SSH status check independently reported Node-RED, the paired GPIO daemon and Hub active, with GPIO26 and GPIO20 output/high.

This establishes operator-observed Hub/API unavailability and display recovery for this candidate. At this observation point, sensor-fault or stale telemetry while the API remained reachable, Wi-Fi interruption, process crash/hang and physical valve fault outcomes were unverified. The later Wi-Fi report and test-scope decision below update only their stated scope.

## Roon and shower-page regression — operator observation

After the Hub recovery test, the operator was asked to switch to Roon, check play/pause, change volume by one encoder step and back, then return to the shower page and check temperature and OFF, keeping 24 V valve power disconnected. The operator replied `vse deluje bp` (everything works without problems), confirming all three checks for the installed Pi source and flashed dial artifact recorded above.

This is operator-observed transport, volume and page-return evidence. No exact observation time, volume values, screenshot or independent bridge trace was supplied. The OFF indicator is UI evidence, not relay-contact or physical valve-position feedback. No SUPPLY action was requested in this check.

## Wi-Fi observation and operator test-scope decision

The operator reported that Wi-Fi interruption worked during several accidental disconnections in the current candidate session. Record Wi-Fi interruption/recovery as `VERIFIED_BY_OPERATOR`. The exact interrupted component, count, duration, transition timing and logs were not supplied; this report does not establish physical valve behavior during an outage.

The operator explicitly chose to skip the sensor-fault test because temperature is informational, and reported that a lower-temperature test is currently not possible. This decision updates the current display-only acceptance scope:

| Check | Status | Basis and limit |
| --- | --- | --- |
| Ordinary shower temperature and page placement | VERIFIED_BY_OPERATOR | Earlier shower-only observation; one decimal comma |
| Hub interruption and display recovery | VERIFIED_BY_OPERATOR | Earlier bounded stop/restart check |
| Roon play/pause, volume and shower-page return | VERIFIED_BY_OPERATOR | Earlier confirmation of all three checks |
| Wi-Fi interruption/recovery | VERIFIED_BY_OPERATOR | Operator report of several accidental interruptions |
| Physical sensor-fault / individual-lead test | SKIPPED_BY_OPERATOR | Waived for the current informational display scope; behavior remains unverified on hardware |
| Lower-temperature comparison | DEFERRED_UNAVAILABLE | Currently not feasible; accuracy at lower temperatures remains unverified |

Skipped and deferred checks are not passing tests. The sensor-fault test is no longer a required immediate step for this display-only scope; the lower-temperature check is deferred. This decision does not commission automatic control, modify `sensor_commissioned`, or establish hydraulic fault outcomes.

## v1.0.0 release authorization

The operator requested release v1.0.0 after accepting the informational display
scope and the recorded skipped/deferred tests. [Release scope and component
identities](releases/v1.0.0.md) distinguish the tested dial binary, Pi package
metadata update and remaining evidence limits. Earlier draft statuses describe
their historical checkpoints; current publication state is on GitHub.


## Admin candidate USB and IPv4 observation — 2026-10-02

The operator kept valve 24 V disconnected and continued the staged installation. Pi source `52c0815c17ecd94d4939c2995a0cced3a40be8bc` was installed through the trusted operator console: 20 installed Python files matched source, manual_timed/MANUAL_DRAIN and duration capability were reported, protocol 2 DRAIN succeeded, and all three services were active with GPIO26/20 output/high. Restricted SSH independently confirmed the checkout, services and output levels. This does not establish physical valve position.

The same ESP32-S3 rev v0.2, MAC `d0:cf:13:1e:15:44`, was identified on COM3 with 16 MB flash and 8 MB embedded PSRAM. Two full-flash reads failed with short USB packets before any flash write. After the operator changed the USB connection, esptool 4.12.0 at 460800 completed a 16,777,216-byte backup with its device/host MD5 comparison; SHA256 `39a2fe947b782b36990c24d596455024bc66b894f4b5482ec77cc3aa37a5388e`. The protected backup includes NVS; no credential contents were printed. Its factory application matched the tested v1.0.0 artifact, and its partition table matched the candidate. The changed connection coincided with successful reading; a specific cable defect was not established.

Candidate source `d0b1a2836dc6128972b3293282861efb11b231fc`, app SHA256 `f3c308b45a59d259a58e52531d866d9dbb90e61b877c6935b645c72977b95dbf`, was written in four regions. A separate verify_flash matched all four digests. A separate NVS read before boot was byte-identical to the backup. No whole-chip erase was used. A 50-second boot observation recorded one startup, ELF prefix `0119fde19`, ESP-IDF v5.5.5, IP `192.168.114.173`, and valve worker free-stack watermark 5864/12288. No selected panic/assert/overflow/brownout/watchdog markers appeared among 275 examined lines. Raw serial logs were not saved.

GET `/admin` returned 200, but GET `/admin/api/session` by IPv4 returned 403. This first candidate is not accepted for admin use. ESP-IDF's HTTP server creates an AF_INET6 listener when CONFIG_LWIP_IPV6=y, as in this exact build. The trusted-host check used a sockaddr_in buffer and accepted only AF_INET. The follow-up uses sockaddr_storage, validates returned sizes, and recognizes only IPv4-mapped IPv6 addresses as IPv4. Native IPv6 addresses are not truncated into trusted IPv4 addresses; Host/Origin and PIN requirements remain in force.

A production-handler regression with an IPv4-mapped local socket failed at the expected first-session 200 assertion before the fix. Afterward, admin HTTP, the complete admin suite, valve suite, controller dependency policy and git diff --check passed. Tests also reject unrelated IPs, host suffixes, wrong ports, foreign origins and native IPv6 addresses with matching trailing IPv4 bits. Firmware build, review and the follow-up hardware observations are recorded separately; these host checks do not establish PIN setup, saved duration, rotation performance or actuator behavior on the device. No timed/SUPPLY action or Roon playback mutation occurred in this installation check.

### Mapped IPv4 follow-up artifact

Source `9b1ca059f0142098d2f510637351941d842f4979` built successfully with the same ESP-IDF 5.5.5 target/profile after commit. Application size is 2,129,600 bytes, SHA256 `38e29badcf6e80927ede5acb955204ff1cc65b39a4d971eba2b6271d8bd25c58`, ELF SHA256 `6c2f753a41f173f65b15289644c69f44341d97266991399a400d7c18a5838673`. Independent review approved the patch with no blockers; the final guard preserves IPv4-only compilation by source inspection, and the host fixture now mirrors lwIP truncation semantics. Socket-failure branch coverage and an actual IPv4-only build were not added.

The current 16 KiB NVS was saved again before the follow-up and matched the original NVS. Four firmware regions were written and separately digest-verified with esptool 4.12.0. Another read proved NVS byte-identical before boot. During 50 seconds from `2026-10-02T19:47:50.526397+00:00`, observation recorded one startup, matching ELF prefix `6c2f753a4`, restored IP `192.168.114.173`, and minimum observed valve-worker watermark 5832/12288. No selected fault markers appeared among 276 examined lines; this bounded observation is not long-term stability proof.

GET-only checks returned `/admin` 200 with the setup form, `/admin/api/session` 200 with setup_required=true/authenticated=false, unauthenticated settings 401, and a foreign Host on the session route 403. The IPv4 403 defect is therefore resolved on this exact hardware artifact. Restricted Pi status after these checks again returned three active services and both outputs high. The operator was asked to choose a private PIN, retain the recovery code and confirm the settings controls. PIN creation/login, duration persistence, four-angle touch/rendering/performance, reboot persistence, Roon regression and production authentication scheduling remain pending until separately observed. No timed/SUPPLY action was performed, and 24 V remains disconnected by operator confirmation. Both PRs stay draft; no new release or merge occurred.
