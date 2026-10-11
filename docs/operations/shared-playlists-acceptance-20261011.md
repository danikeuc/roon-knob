# Shared playlists and QGSQ: device acceptance, 2026-10-11

## Installed artifact and provenance

Dial app SHA256: `ff6d0b8991233e1b2362a447b00cb76d10a331a4f3c1f95a2664dc1611daa6b9`.
The application was installed on the verified ESP32-S3 over COM3 with matching
readback and preserved boot/NVS prefix. A private full-flash backup is retained.
The app remains on branch `codex/weather-playlist-dial`, based on `358fff1`,
with pre-existing uncommitted work. HEAD alone does not identify the deployed
binary; retain the candidate manifest and tracked source patch.

Companion bridge identifies itself as
`8abe29d455087a01552c71cbcdfaff7fa95df99d+shared-playlists-e855c738e230`.
Operator cutover reported shared playlists enabled with runtime settings and
credentials preserved. This note is an acceptance record, not a release.

## Acceptance evidence

The operator reported: "sedaj deluje, tudi playliste po conah" (it now works,
including playlists across zones). Record BLE operation and shared playlists
across zones as operator-confirmed after the controlled Dial restart.
Serial independently confirms connection, encryption, HID subscriptions and
mapped Play/Pause and Volume Down input. It does not prove audible playback.
The complete five-key, named-zone and downward-swipe matrix was not individually
reconfirmed in this closing test; do not silently promote those checks to PASS.

## Known open BLE reconnect defect

Before recovery, QGSQ was visible in scans but every pair attempt logged
`ESP_HIDH: Already Connected`. The SDK rejects an address already in its device
list before starting a new radio connection; the admin page meanwhile offered
pairing and reported CONNECT. A retained SDK device object is confirmed.
The initial event that left that object behind is not established.

A controlled restart restored pairing and operation without a firmware update
or operator flash/NVS writes. This is recovery, not a permanent defect fix.
Review identified candidate lifecycle gaps during discovery/disconnect and
incomplete asynchronous cleanup; these hypotheses require reproduction before
being called the cause. Reconnect after sleep, repeated forget/pair and durable
prevention remain unverified. Do not classify the issue as fixed or regression-free.

## Closeout

User requested session closeout. Diagnostic serial capture has ended. Existing
source changes, deployment packages and private rollback material are preserved.
No merge, push, tag, release or permanent BLE firmware patch was performed in
this diagnostic closeout. Full integration verification is still required before
merging or publishing the accumulated development changes.

Fresh narrow checks: production QGSQ input dispatch and report mapping PASS;
BLE lifecycle policy host checks PASS after correcting the local compiler include
path. These host checks do not reproduce the retained SDK object on hardware.

Local evidence directories (under the Codex artifact root):
- `shared-playlists-device-update-20261011/acceptance-status.json`
- `ble-connect-diagnosis-20261011/finding.json`
- `ble-connect-diagnosis-20261011/connection-capture-v2.jsonl`
- `ble-connect-diagnosis-20261011/restart-capture.jsonl`
