# Independent light on the Dial

Source implementation: 2026-10-13, branch `codex/independent-lighting-design`. This is committed software, not evidence that the new firmware is installed or the third relay is commissioned. The recorded installed Dial remains `v1.3.0-shower.3` until a separately approved deployment and exact artifact hardware validation.

The authoritative policy and Pi/Node-RED contract remain in the [canonical Pi lighting specification](https://github.com/danikeuc/rpi-freez-protect/blob/codex/independent-lighting-design/docs/superpowers/specs/2026-10-12-independent-lighting-design.md). Dial submits intent and displays controller observations; it owns no GPIO or lamp safety policy.

## Configure the independent connection

Open PIN admin and choose **Luč**. Supply its own base URL, for example `http://pi:8081`, and two distinct 32–512 character ASCII credentials: the light display token for status/actions and the light settings token for settings/history. The Pi/gateway must already expose the exact `/api/v1/lights` API with those roles. These credentials are independent of the shower and camera token. Both are write only, clear from the form after submission/session loss and persist in the independent `light_private` NVS namespace. Changing the URL requires entering both roles again. Blank browser token inputs preserve the existing connection. Saving connection/settings and reading history never activates the light.

The UI becomes available after configured status advertises `independent_light_v1`. Older/unconfigured Pi installations retain two pages and admin explains unavailable/setup state. There is no shower API fallback. Actual Protect source IDs, field paths and authentication are configured and verified on the Pi ingress; the current status DTO does not claim to inspect that profile.

## Screen and actions

Horizontal swipes navigate Roon, Tuš and Luč in both directions. AUTO taps request timed ON/OFF while retaining AUTO. Explicit manual controls use the confirmed configured duration; each manual command renews the full override. Manual copy remains **ROČNO — kamera začasno izključena**, **AUTO čez 42 min**, and **Nazaj na AUTO**. A vector bulb and M stay on Roon/Tuš during confirmed manual mode. Offline/local-zero timers retain unknown manual state until fresh Pi status confirms AUTO. GPIO readback describes the output, not lamp illumination.

Admin includes confirmed mode, controller output, whole minute hold/manual durations (1–120), explicit controls and bounded read-only history. A draft slider value never changes the duration shown on a control until confirmed settings are read. Settings changes affect subsequent intent and do not change an active deadline. Failed settings-role reads retry at most once every fifteen seconds and cannot starve the five-second status poller. OFF and return to AUTO use fresh status without requiring settings availability; manual activation requires a known duration.

## Local intent and reply binding

Each admin status/settings/history/action request supplies `X-Light-Config-Generation` from its confirmed connection view. This is local Dial metadata and never enters the strict Pi JSON body. Missing/malformed metadata returns422; an obsolete or in-progress generation returns409 without upstream I/O. The captured generation survives HTTP admission, asynchronous handoff, worker submission and final reply. A tab holding controller A's revision cannot submit to newly configured controller B, even when B has the same revision. Config/Wi-Fi/session changes cancel obsolete work; timeouts never automatically retry mutations.

Networking runs on a separate bounded job worker. Admin hands requests off asynchronously so upstream delays do not occupy the main HTTP task. A six-second admin wait cancels retained work safely; late completion cannot publish a result to a expired request. The worker rechecks its five-second deadline before body transmission and each read, with at most two-second socket timeouts. A library DNS stall can occupy that separate worker until DNS returns; it cannot send an expired action afterwards. This does not establish measured hardware timing.

Roon volume, playlists, zone gestures, shower controls and QGSQ/BLE mappings remain unchanged. No BLE light mapping is included.

## Verification and installation boundary

Run `IDF_PATH=<ESP-IDF5.5.5> sh scripts/test_light_dial.sh` for native DTO/config/queue/UI/socket/admin tests. Browser coverage is `node tests/admin_dial/test_light_admin.cjs`, alongside existing admin redesign/history tests; `PLAYWRIGHT_MODULE`, `BROWSER_PATH` and `DIAL_SOURCE` may select the available runtime. `python3 scripts/embed_admin_page.py --check` verifies the generated header. Native/browser suites are wired into CI. Build the existing Waveshare ESP32S3 target with exact ESP-IDF5.5.5; a green build does not validate flashing or physical behavior.

Commissioning still requires exact-artifact flash/boot/display/touch/encoder/Wi-Fi acceptance, isolated relay wiring and observed lamp behavior, boot/crash/power timing, and real authenticated Protect event profile/source IDs. The owner reports LED lighting, maximum20W at220V; contact/driver and board suitability remain unverified. Dial loss cannot extend a controller timer or replay old commands; Pi/executor lease enforcement owns the output. Executor hang and power/reset guarantees remain physical evidence gates. Existing paired GPIO26/20 behavior and published releases remain unchanged.

## GPIO_SHARED_FSEL2_COEXISTENCE — controller activation blocked

**Activation remains blocked with the installed legacy paired mmap daemon.**
GPIO20/21/26 share FSEL2; the legacy configure path can overwrite a concurrent
kernel GPIO21 update using its stale register snapshot. The separately
authorized repair now removes userspace FSEL access from candidate source:
exclusive kernel reservation/configuration for GPIO26/20 surrounds restricted
single-mask GPSET0/GPCLR0 writes and readback. See the
[ownership migration runbook](https://github.com/danikeuc/rpi-freez-protect/blob/codex/independent-lighting-design/docs/operations/paired-gpio-migration.md) for exact profile, interpreter,
dependency, device mapping, lifecycle and rollback requirements.

This is a source repair, not an installed migration or physical acceptance.
Keep lighting templates, flow and gateway disabled. Do not create
/etc/independent-light/commissioned, set LIGHT_GPIO21_ENABLED=commissioned or
grant light DeviceAllow until exact installed paired migration plus disconnected
startup/request/release/crash acceptance is recorded. Kernel-only configuration
resolves the source FSEL writer race; it does not establish atomic initial
direction changes, crash protection, physical independence or valve position.
The current installed Pi/Dial records and all lamp/wiring/Protect gates remain.
Initial ordering or a userspace lock alone cannot repair the legacy writer.

Browser status and settings reads now fail independently: settings503 preserves fresh status/revision for deliberate AUTO OFF or Back to AUTO, clears confirmed durations/settings revision and gates duration-dependent activation. Config/session fences and the ten-second freshness limit continue to apply.

## Current release status — 2026-10-18

The earlier source-only activation block above is historical. Owner receipts now record the paired migration, independent-light runtime and G4 Pro forwarding installation; see Pi docs/PROJECT_STATE.md and docs/releases/v1.4.0.md for current scope and limits. Manual ON expiry includes a 900-second camera cooldown before AUTO. The light page uses confirmed GPIO state for its icon and owner-tested change-driven rendering for smooth swipes. Release-candidate hardware validation is tracked separately in release-v1.4.0.md.
