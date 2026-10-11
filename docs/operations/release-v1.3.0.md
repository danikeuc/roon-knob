# Dial v1.3.0

Companion release for Pi v1.3.0. Includes persistent shower-history admin view,
previously accepted modern admin/forecast UI, shared playlist selection across
Roon zones, zone navigation and QGSQ media-button handling.

The shower history requires Pi v1.3.0 and its nginx GET route. Existing weather
credential is reused. History begins after Pi installation. Reads are passive.
See [history behavior and validation](shower-history.md) and
[earlier operator acceptance](shared-playlists-acceptance-20261011.md).

Known limit: intermittent BLE reconnect after a disconnect remains open; a
restart restored the same prior firmware. This release does not claim that
permanent repair, exhaustive five-key retest, hydraulic acceptance or fault
injection. Prior user acceptance and new exact-artifact installation receipts
are separate evidence. Existing tags are preserved; release targets the owner's
fork, never upstream muness/roon-knob. No NVS erase or settings reset is intended.
