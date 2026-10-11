# Changelog

## [v1.3.0-shower.3] - 2026-10-11

# Shower history and companion controls

Adds Tuš / Zgodovina tuša with local time, operation, command, reason, refresh and
ten-record pages. Requires Pi history package 1.3.0 and existing weather credential.
Includes the previously operator-accepted shared Roon playlists/zones and admin
redesign. No new BLE reconnect fix is claimed.

Source: 69cab63b252fca2a1dc25e2c3aaf9153badceb37
Tag: v1.3.0-shower.3 (historical v1.3.0 and earlier candidate tags preserved).
CI: https://github.com/danikeuc/roon-knob/actions/runs/38151787261

The exact CI binary and device installation receipt are attached below.
On 2026-10-11 the operator confirmed that shower history is visible in admin.
No Synology or valve configuration changes are part of this firmware update.

Known limitation: intermittent QGSQ reconnect was previously recovered by a
same-firmware restart. Do not describe it as permanently fixed.

PR: https://github.com/danikeuc/roon-knob/pull/14

## Device installation

CI run 38151787261 passed all six build/test jobs. Exact CI app SHA256:
50ce9f7518f8227ef576e3807e647cb64c53ecc7488ab2e868c4c2a0caf6163b.
Installed on the connected Waveshare ESP32-S3 via COM3 at 15:40 UTC.
Full private backup verified; application readback matched; NVS, bootloader,
partition table and OTA metadata unchanged before first boot.
60-second boot capture saw one app boot, no panic or BLE-init failure markers.
Wi-Fi IP log event was not captured; a subsequent admin HTTP200 response and
history markup confirmed network/admin availability. The operator subsequently confirmed visible history in admin. See attached sanitized installation receipts.

PR preview hosting failed with GitHub Pages HTTP404 in run 38152469624.
All six compile/test jobs and the tagged artifact run passed. No preview
hosting success is claimed.


All notable changes to this project will be documented in this file.

## [v1.3.4] - 2024-12-14

### Bug Fixes
- Fix WiFi credentials wiped when bridge URL empty (#11)

## [v1.3.3] - 2024-12-14

### Bug Fixes
- Fix WiFi credential persistence + improve setup UX
- Add save verification and countdown display

## [v1.3.2] - 2024-12-14

### Bug Fixes
- CI: Fix release artifact paths

## [v1.3.1] - 2024-12-14

### Bug Fixes
- Bridge: Fix pairing persistence
- CI: Reduce duplicate builds

## [v1.3.0] - 2024-12-13

### Features
- Bluetooth mode via ESP32 UART (dual-chip architecture)
- BLE HID + AVRCP controller support
- Exit Bluetooth confirmation dialog

### Bug Fixes
- Zone selector overlay hides before mode change
- WiFi provisioning reboots after saving credentials
- On-demand BT activation

## [v1.2.12] - 2024-12-10

### Features
- WiFi provisioning via captive portal (SoftAP)
- mDNS bridge discovery
- Album artwork display

### Bug Fixes
- Volume overlay visibility
- Font rendering improvements
