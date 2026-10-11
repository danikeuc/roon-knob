# Admin and weather presentation candidate, 2026-10-05

Tracking: danikeuc/roon-knob#13; dependencies #9, #11, #12.

The owner approved the four-section dark design (Tuš, Roon, Zaslon, Varnost).
`web/admin_dial.html` is now the canonical embedded admin page. Regenerate its
C header with `python3 scripts/embed_admin_page.py`; CI/local checks should run
the same command with `--check`. No external JavaScript/CSS/fonts or persistent
browser credential storage are introduced.

## Effective behavior

- Existing session, CSRF, generation and revision checks remain authoritative.
- The five-day cards show actual Pi minima only for available eligible/cold
  forecasts. Unsupported/missing optional weather codes show no condition icon.
  Unavailable/stale data clears current forecast values; historical minima remain
  labelled in the details. USER_OFF remains visible and never implies AUTO.
- Admin pipe temperature uses validated Pi sensor telemetry; unavailable means —.
- The Dial keeps the shower symbol and touch controls, adding tomorrow's minimum
  and a vector weather icon. Missing/stale/disconnected observations clear these.
- New playlist selection begins only after a successful catalog read. It replaces
  saved favorites only on save. This deliberately respects the bridge requirement
  that all newly selected entries share one catalog session. No identity inference
  from display names is permitted. Search applies to loaded pages; Naloži več
  continues the same catalog. At most five items can be selected.
- Failed saves preserve the draft. Conflicts require explicit rereading/reselection;
  no automatic mutation retry. Late responses cannot replace a new login's draft.
- Duration remains 1–10 minutes subject to confirmed Pi capability; rotation remains
  0/90/180/270 degrees. Settings do not start playback or a shower.
- Preserve implemented recovery-code behavior. The prototype's physical PIN-reset
  caption was not an implemented capability and is not advertised in this build.

## Validation and boundary

Host suites: admin/auth/crypto/settings/HTTP/weather runtime, valve parser/UI/runtime,
playlist persistence/UI/isolation; browser suites test existing authentication and
new navigation, pagination, provenance, missing data, failures and auth races.

Browser commands use `PLAYWRIGHT_MODULE` and optionally `BROWSER_PATH`:
`node tests/admin_dial/test_admin_browser.cjs` and
`node tests/admin_dial/test_admin_redesign.cjs`.

Source review closed three findings: null subtitles, mixed catalog provenance and
late browse responses after login. ESP-IDF 5.5.5 ESP32-S3 build passed with app size
0x220390 in a 0x280000 partition (15% free). Exact-artifact hardware acceptance and
deployment are pending. This candidate includes pre-existing local playlist
contrast/accepted-command wording changes; its manifest must preserve their
provenance instead of attributing them to the UI redesign.

The Pi's optional WMO codes need its separate candidate update to provide condition
icons. Older Pi data remains usable for minima; no fabricated sunny/rainy icons.

## Shower history

The Tuš tab also provides a persistent transition history. See
[Shower operation history](shower-history.md) for its read-only contract and
installation/verification boundary.
