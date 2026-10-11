# Shower operation history

## User behavior

In **Tuš → Zgodovina tuša**, open the history card to read controller transitions.
Rows show the local date/time, automatic/manual/stopped operation, commanded
on/off state and reason. Ten newest records are shown per page; **Starejši** and
**Novejši** navigate, **Osveži dnevnik** restarts at the newest page. No automatic
polling or write action is attached to this card. Browser timezone is displayed.

The Pi stores dedicated transition rows durably in its existing SQLite audit
DB. Repeated identical operation/command pairs do not create new rows. Startup,
orderly shutdown and faults are also recorded. History begins after installing
the Pi update; there is no reconstruction of older events. A sudden power loss
cannot log a shutdown. These are controller states, not measured valve positions.

## Contract and boundaries

Browser GET `/admin/api/shower-history?offset=N` requires a valid PIN session.
Dial calls Pi GET `/api/v1/display/history?offset=N` with the existing private
weather-settings credential. Response contains `items`, `offset`, `has_more`.
Each item has `occurred_at` UTC, `mode`, `operation`, `command`, and `reason`.
Offset is 0..1000000, fixed page size 10, body below 4096 bytes.

The proxy shares the existing weather worker and bounded admission/deadline.
Response parsing rejects oversized/malformed data and projects only the defined
fields. Late responses after session/config changes cannot update the view.
History is cleared on auth loss or page hiding. Empty history is distinct from
unavailable storage, an older unsupported Pi, or a failed connection.

Offset pages are not a frozen snapshot: new events can move records between
pages. Refresh returns to the newest entries. No retention/deletion is added.
Pi read handling never invokes live status (which can expire a timer), weather
fetching, relay actions or settings writes. History writes retain the existing
fail-closed audit persistence policy; a disk error can prevent its own fault row.

## Deployment scope

Requires coordinated Pi package + nginx history route and Dial application
firmware. Bridge/Synology changes are not required. Reuse the existing weather
credential; do not create another secret. Current packages retain their prior
version numbers and are development candidates, not a new release.

No deployment or hardware acceptance is claimed by this implementation. Follow
the project commissioning procedure for backups, disconnected valve power,
DRAIN verification, paired GPIO and firmware installation. After installation,
verify startup rows survive a Pi restart, then observe a separately authorized
manual/AUTO transition and read it in admin. Never activate a shower merely to
populate history without authorization.

## Source verification 2026-10-11

- Pi: full pytest: 666 passed, ruff, mypy: 26 files, isolated wheel/sdist build.
- Dial: full `scripts/test_admin_dial.sh`, including actual worker timeout,
  cancellation, admission, auth, offset and malformed response tests.
- Browser: `test_shower_history.cjs`, `test_admin_browser.cjs`,
  `test_admin_redesign.cjs` pass. Redesign mock gained the already-existing zone
  API fixture so it follows the current admin page contract.
- Generated page: `scripts/embed_admin_page.py --check` passes.
- ESP-IDF 5.5.5 ESP32-S3 application builds within the existing app partition.
- Independent proxy-agent UI review and root Pi/proxy review: no blocking
  findings; explicitly reviewed read side effects, persistence failure, stale
  sessions and bounded storage/transport. No physical acceptance claimed.

New browser test initially failed on the absent history card before UI changes.
It covers local timezone, pagination, read-only requests, escaped text, empty vs
unavailable/malformed data, late response after logout and mobile width.

Engineering handoff: **READY_FOR_INDEPENDENT_REVIEW**; installation and exact
artifact hardware acceptance remain separate. Existing unrelated local source
changes are preserved and included in the candidate firmware baseline.