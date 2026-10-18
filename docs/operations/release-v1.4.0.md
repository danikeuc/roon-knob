# v1.4.0-sauna.2 — Independent light and smooth navigation

Companion to Pi v1.4.0, published only on the owner fork danikeuc/roon-knob.

- Independent light page with vector ON/OFF bulb, confirmed AUTO/manual mode and duration, countdown and manual reminder.
- Light admin connection, separate credentials, duration settings and bounded history; asynchronous request ownership and refresh recovery.
- Light-page rendering only invalidates changed visual state. Local tests and owner acceptance confirm smooth navigation for the pre-release candidate b255df14ac77a3bcab7a08f1e9fbb882338a145b8d34df067aa7282dfc3a0fd9.
- BLE startup readiness and bounded networking improvements retain the existing Roon/shared-playlist features.

Release verification builds only the Waveshare ESP32-S3 Dial using ESP-IDF 5.5.5 and the tested static Wi-Fi TX buffer profile. It does not publish other boards or overwrite generic web-flasher channels. The exact versioned CI artifact was flashed, read back and physically accepted by the owner on 2026-10-18.

App-only updates use 0x10000 on the verified 16 MB partition layout. Preserve NVS and credentials; do not flash a merged/factory image over a configured device. Lamp/valve physical acceptance and controller historical fault limitations are documented in Pi v1.4.0. No general BLE reconnect guarantee is claimed.

See docs/esp/light-swipe-20261018.md for measured input evidence and prior installation receipts.

Candidate tag v1.4.0-sauna.1 was not published: identity inventory omitted the copied compatibility aliases and owner release provenance URLs. Candidate .2 classifies only those exact workflow/doc paths; runtime source is unchanged apart from the version.

## Exact CI artifact

Source: `30fb2df0a5d930a2e61fbef44622ec0a043ecb55`. All four jobs in [CI run 39133878054](https://github.com/danikeuc/roon-knob/actions/runs/39133878054) passed.

App SHA256: `04455266b50b3484f2f9b56fcc892e663a5c2b5ba5c43efac551e40363ed15b2`. Exact CI image flashed and verified on the owner Waveshare ESP32-S3; NVS byte-identical and admin HTTP 200 after boot. Owner confirmed display/navigation operation ("deluje") on 2026-10-18 after this exact CI image was installed.
