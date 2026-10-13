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

Do not enable the candidate Pi GPIO21 executor alongside the current mmap paired daemon. GPIO20/21/26 share FSEL2; the existing paired daemon reads/writes a whole-register configuration snapshot while gpiod21 request/release or kernel cleanup can overlap it. The new fixed-line API does not establish whole-system physical isolation. This known source-level configuration race has no physical acceptance evidence yet.

Keep the candidate lighting deployment disabled. Do not create `/etc/independent-light/commissioned` or set `LIGHT_GPIO21_ENABLED=commissioned` until separately reviewed architecture eliminates uncoordinated shared-register updates and regression/physical evidence covers startup/restart/request/release/crash behavior, preserving paired atomic output and leases. Initial service ordering or a userspace lock alone cannot cover kernel cleanup; no paired backend rewrite is included. See the [canonical blocker](https://github.com/danikeuc/rpi-freez-protect/blob/codex/independent-lighting-design/docs/superpowers/specs/2026-10-12-independent-lighting-design.md#gpio_shared_fsel2_coexistence). This candidate is source delivery, not installable/enablable while the gate remains open.

Browser status and settings reads now fail independently: settings503 preserves fresh status/revision for deliberate AUTO OFF or Back to AUTO, clears confirmed durations/settings revision and gates duration-dependent activation. Config/session fences and the ten-second freshness limit continue to apply.
