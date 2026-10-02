# Dial admin settings — provisional operations

**Status:** issue #9 / draft PR #10 remains a candidate. The first image was
flashed and boot-observed on 2026-10-02, but admin access by IPv4 failed
with HTTP 403; it is not accepted. The mapped-address fix from source `9b1ca05` was subsequently flashed and
passed digest, NVS retention, bounded boot and read-only HTTP checks. User
PIN/settings/rotation acceptance remains pending. See the [commissioning record](../valve-dial-commissioning.md#admin-candidate-usb-and-ipv4-observation--2026-10-02). This page covers
PIN/recovery, shower duration and rollback for the Waveshare ESP32-S3 Dial
candidate built with ESP-IDF 5.5.5. The user explicitly confirmed
0°, 90°, 180° and 270° for this candidate. Device performance and
interaction acceptance at those angles remain pending. Historical v1.0.0
observations remain scoped to the older artifact in [the release record](../releases/v1.0.0.md).

## Phone setup on the trusted LAN

Find the Dial's current local IP by the existing device/network procedure and
open `http://<dial-ip>/admin` from a phone on the same trusted LAN. The server
uses local HTTP and a device-local PIN, not internet authentication. Restrict
this network to trusted clients. Do not put a PIN, recovery code, session
cookie or display token in screenshots, tickets or logs.

At first setup choose exactly four ASCII digits, including an initial zero if
desired; there is no default PIN. Confirm it in the page. Record the newly
issued recovery code in a secure offline or password-manager location available
to the owner. The code is shown only when issued; issuing another invalidates
the old one. Log in with the PIN for settings. The session expires after 15
minutes of inactivity; five incorrect attempts lock the device for 60 seconds,
including across restart. Log out when finished.

Use the authenticated page to select a whole-minute shower duration from 1 to
10, with 10 as the default. Wait for confirmed saved values; after an uncertain
response, reload and use the read-back value. A new duration affects the next
accepted shower, never an active Pi deadline. Existing PIN protection covers
ordinary configuration and AP provisioning mutation paths. Viewing settings,
PIN changes and duration saves must not issue SUPPLY.

If the PIN is forgotten, use the saved recovery code to set a new PIN. A
successful recovery issues a replacement recovery code and preserves other
settings. If both PIN and recovery code are lost, there is no web bypass.
Escalate to deliberate physical recovery with an NVS/config backup and a
review of what reset would erase. An interrupted first setup after the
persistent established marker may also need deliberate recovery; a missing
credential record must not reopen setup. Firmware boot does not silently erase
NVS on initialization failure. Never use a whole-chip erase as routine
reinstall or rollback.

## Local display rotation

The authenticated phone page offers 0°, 90°, 180° and 270°. The candidate
stores the chosen angle as a local override. Before an override is saved, the
existing bridge/charger path retains its legacy 0°/180° behavior; afterward
that path does not replace the locally saved angle. A confirmed save applies
the new angle to the display and its inverse touch coordinates, so touch,
swipes and valve/Roon gestures use the same orientation. An in-progress touch
or valve hold is cancelled during a change and stays suppressed until release;
changing rotation must not issue START or alter an active Pi deadline.

The source uses bounded rotation geometry and PSRAM scratch, then copies each
rotated block back into the internal DMA-capable display buffer and drains the
LCD transaction before that buffer is reused. The PSRAM scratch is not handed
to DMA. Host tests cover all four transforms, production flush/input functions
with faked LCD/touch, and settings application/rollback. These checks do not
measure real frame time, UI responsiveness, heap or stack on the Dial. Keep the
candidate draft if the screen freezes or the memory gate fails at any angle.
The [rotation decision](../meta/decisions/2025-12-20_DECISION_ROTATION.md)
retains its historical 0°/180° rationale and records this candidate update.

## Pi compatibility and safe use

Install and validate the compatible Pi API before this Dial firmware. Fresh
`timed_shower_duration_supported: true` in `manual_timed` lets the Dial submit
an explicit 60–600-second whole-minute request. The old Pi accepts the legacy
empty 600-second request; a saved short duration blocks START if capability is
missing, stale or false. The page retains the saved value and explains the
block. Select and confirm 10 minutes on a supported setup or restore the new
Pi API; never bypass the block or replay a timed request after reconnection.
Early DRAIN remains available. An accepted HTTP request or GPIO readback does
not prove valve movement. The Pi paired daemon remains the only GPIO writer.
Its legal requested pair levels are high/high DRAIN and low/low SUPPLY.

The Dial records monotonic time when it queues START. Its worker rejects an
unsent START at age 10,000 ms or more, including when the deadline is crossed
while fetching preflight status. A backwards clock also rejects START. Rejection
is reported as a timeout; DRAIN has no age limit. Disconnect clears queued
commands, and reconnect fetches status without replaying START. Once POST has
been handed to the transport, a later response is reconciled even if the Dial
clock passes 10 seconds. This Dial guard cannot impose a network delivery
deadline or cancel a Pi interval that was already accepted. The Pi timer keeps
its original deadline through ordinary Dial Wi-Fi loss.

## Candidate install and rollback preparation

Before any separately authorized flash, save the current Dial NVS/config using
the reviewed operator procedure without displaying credentials. Record source
commit, target/profile, application SHA-256, bootloader/partition/OTA region
identities, running image and the old verified bundle. Retain protected Pi
config/environment and Node-RED flow backups separately. Keep the 24 V valve
supply disconnected unless the same bounded physical test is explicitly
approved. Flash an exact reviewed candidate only after Pi compatibility gates
pass; verify affected flash regions, boot, settings retention, Roon and input
before acceptance. No flash command is part of this candidate runbook.

For rollback preserve the [v1.0.0 four-region bundle](../releases/v1.0.0.md)
and its manifest, with tested application SHA-256
`c2abd6d1168cac05d3eeb1581fd11d7c64c6e91a43644878584f148c8f4e4b00`.
That tested binary reports inherited `2.5.3-valve.1` internally; identify it
by hash. Preserve NVS. The older firmware may not enforce the new web PIN on
its admin surface, so restrict it to the trusted LAN and review access before
rollback. A rebuild of the tag is a different artifact. Candidate and
baseline identities are in the separately generated candidate manifest.

## Acceptance after separate authorization

- On a phone: initial zero PIN, login/logout, timeout, lockout across restart,
  single-use recovery and lost reply, plus retention of existing Wi-Fi/Roon and
  valve configuration. Do not record real credentials in the evidence.
- At 0°, 90°, 180° and 270° on the exact image: check Roon controls and
  artwork, swipes, touch, valve hold cancellation, overlays and charger
  behavior. Record free heap/stack and compare 90°/270° flush time and response
  against 0°/180°, with user acceptance. A host test or compile does not pass
  this physical gate.
- In a separately approved disconnected-load envelope: one- and ten-minute Pi
  intervals, no extension on repeated START, next-interval setting update,
  early DRAIN, request/response/deadline and paired GPIO trace. These do not
  establish hydraulic routing.
- Verify Wi-Fi loss and Dial restart without replay; old/new compatibility in
  both directions, including a saved-short-duration Pi rollback. Mark any
  unperformed check pending, never passed by a build or mock browser run.

| Fault domain | Candidate mechanism | Remaining device/physical proof |
| --- | --- | --- |
| Dial-to-Pi communication loss | Dial discards queued START on reconnect. An already accepted Pi deadline continues under Pi authority. | Timed deployed trace and physical routing remain pending. |
| Hub-to-daemon renewal loss | Daemon lease requests DRAIN when renewal stops. | Deployed timing and physical behavior remain pending; historical renewal/reassertion concern requires review. |
| Hub process crash/hang | The paired daemon lease is the intended independent DRAIN enforcer. | Stalled-process timing, relay and valve behavior remain pending. |
| Controller reset/reboot | Pi and Dial restore conservative state; Dial requires fresh capability before short START. | Pre-app boot pin levels and valve behavior remain pending. |
| Power loss/restoration | Software starts conservatively when able to run again. | Controller, relay/driver and 24 V actuator supply outcomes each remain unknown; OFF or high/high does not prove water routing. |
