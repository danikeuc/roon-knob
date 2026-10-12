# Planned independent light on the Dial

Date: 2026-10-12. Design-only; no firmware, admin route or hardware change.

The owner-approved behavior and Pi/Node-RED protocol constraints live in the
[canonical Pi lighting specification](https://github.com/danikeuc/rpi-freez-protect/blob/codex/independent-lighting-design/docs/superpowers/specs/2026-10-12-independent-lighting-design.md). Do not duplicate the
policy in firmware or the Roon/Synology bridge.

## Screen and state

Add a third page Luč alongside Tuš and Roon. In AUTO, a normal tap requests timed
ON or immediate OFF without changing mode; later fresh camera detections remain
allowed. Only an explicit manual selection suspends camera control. Manual ON
and manual OFF both show a warning and the Pi-confirmed time until AUTO.

Exact localized copy:

- **ROČNO — kamera začasno izključena**
- **AUTO čez 42 min** (example remaining time)
- **Nazaj na AUTO**

Show a small bulb with M on every main page while manual override is confirmed.
On stale/disconnected status, show uncertainty instead of claiming AUTO or OFF.
The manual timer is owned by the Pi; a local countdown reaching zero only causes
a state refresh. Default manual return is 60 minutes, configurable in admin.
Five-minute AUTO hold is a proposal awaiting the owner's duration selection.
Returning to AUTO clears old holds and requests OFF; fresh input is needed for ON.

## Integration boundaries

The light page must not consume Roon volume/playlist gestures, zone selection,
shower controls or QGSQ media events. No BLE lighting mapping in the first scope.
Use separate light credentials and negotiated capability, never a shower action
as compatibility fallback. Old Pi/unsupported status hides or disables the page.
Keep network work bounded/off the UI thread, and discard stale/session-invalid
replies using existing generation fences. Do not optimistically report GPIO or
physical lamp state before confirmed status.

PIN admin gets hold and manual-return duration, confirmed current mode/deadline,
webhook setup status and separate light history. Secrets stay server-side; normal
browser reads and settings changes never send an ON action. Settings affect the
next activation/override, not an already running deadline.

This file carries UI integration requirements only. GPIO21 suitability, fault
response, webhook shape/authentication, deduplication, reset and persistence are
owned by the canonical specification. Its missing hardware/runtime facts remain
unverified. Review the written specification before creating implementation tasks.

## Recorded baseline

Pi 1.3.1 and Dial v1.3.0-shower.3 are already installed. Exact history artifact
and operator acceptance are recorded in [shower history](shower-history.md).
That evidence does not validate this new page or the third relay. Published
release tags and binaries are unchanged by these documents.
