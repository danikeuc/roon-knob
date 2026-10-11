# Independent review: Dial configuration-save HTTP stack

Date: 2026-10-04. Read-only source/artifact review and local host test; no network,
device, credential, relay or playback operation was performed by this reviewer.

## Exact evidence

- Installed source baseline: `4c0a15c08f1ca05fb948cc8c65890ec423eeedf9`.
- ELF: `/mnt/c/users/danik/projects/rpi-freez-protect/dist/dial-weather-playlists-4c0a15c-20261004/hiphi_dial.elf`.
- Existing Windows Xtensa objdump was used to inspect machine-code `entry a1,N`
  prologues and call sites. These are compiled frame sizes, not host C estimates.
- HTTP task budget in baseline `idf_app/main/config_server.c:1010`: 8192 bytes.
- `POST /config` always calls `controller_config_set_endpoint`, including when
  the submitted bridge URL is unchanged. Its storage transaction precedes
  `playlist_private_save`.

## Supported reachable chain

| Frames | Bytes |
|---|---:|
| httpd_thread / httpd_sess_enum / httpd_process_session / httpd_sess_process / httpd_req_new / httpd_uri | 160 + 32 + 32 + 32 + 176 + 64 = 496 |
| config_post_handler / config_post_handler$part$0 | 32 + 1968 = 2000 |
| controller_config_set_endpoint | 608 |
| commit_candidate_locked | 1200 |
| platform_storage_write | 2368 |
| platform_storage_read | 96 |
| esp_log / esp_log_va / vprintf / _vfprintf_r | 96 + 48 + 48 + 800 = 992 |
| __sprint_r / __sfvwrite_r / _fflush_r / __sflush_r / __swrite | 32 + 48 + 32 + 32 + 32 = 176 |
| esp_vfs_write / console_write / underlying esp_vfs_write / usb_serial_jtag_write | 32 + 32 + 32 + 32 = 128 |
| _lock_acquire_recursive / lock_acquire_generic / xQueueTakeMutexRecursive / xQueueSemaphoreTake | 32 + 64 + 32 + 64 = 192 |
| **Total** | **8256** |

The total already includes the 496 bytes of HTTP dispatcher frames. It excludes
task-wrapper/context and other runtime overhead. Do not describe 8256 as being
"before HTTP task frames".

The logging branch requires INFO output through the normal stdio console and
connected USB serial output. The console lock's semaphore path is taken when
the task does not already own that recursive mutex. This is a supported reachable
branch, not a claim that every request follows the deepest branch.

Concrete pointer/call evidence:

- `platform_storage_write` log literal at `0x42000640` is `0x4038c9a8`,
  `esp_log` (96-byte frame), not `esp_log_write` (80-byte frame).
- USB write's lock literal at `0x42000c3c` is `0x403775dc`,
  `_lock_acquire_recursive` (32-byte frame).
- `lock_acquire_generic` has a 64-byte frame and calls
  `xQueueTakeMutexRecursive` on its recursive-mutex task-context branch.
- `xQueueTakeMutexRecursive` at `0x403859b8` has a 32-byte frame and calls
  `xQueueSemaphoreTake` at `0x403858b0` (64-byte frame) when the current task
  is not the owner.
- `_write_r` and `esp_vfs_write` are aliases at `0x4200cefc`; they count once
  per invocation. The console dispatch introduces another underlying write
  invocation; these two invocations are distinct, not two aliases counted twice.

The NVS blob path was also inspected. Its frame sizes include nvs_set_blob 48,
NVSHandleSimple::set_blob 32, Storage::writeItem 128,
Storage::cmpMultiPageBlob 128 and Page::cmpItem 144. No unconditional overflow
claim is based on that chain. The conditional libc `__sbprintf` recursion is
also excluded from the 8256-byte finding.

## Interpretation and limits

P1: the baseline HTTP stack budget is insufficient for the supported chain.
The observed timeout plus subsequent unauthorized session is consistent with
a reset, but it does not establish a stack-overflow crash as the actual incident
cause. No panic/backtrace from the failed request was captured. The source/artifact
finding and the live incident hypothesis must remain separate.

## Patch review

The four-file patch raises only the HTTP task stack to 16384 bytes and records
the value passed to the fake `httpd_start`. Its host regression requires at least
8256 + 2048 bytes. This tests the configured minimum budget; it does not execute
the embedded storage/console chain or measure target high-water usage.

Independent command `. /tmp/freeze-build-tools/env.sh; sh scripts/test_admin_http.sh`
completed successfully: STA routes 28/28, AP routes 25/28, `admin HTTP: PASS`.
Reported RED evidence belongs to the implementer; this reviewer independently
executed the GREEN test only.

Dissent: increasing the stack costs another 8192 bytes of task stack allocation.
It is a bounded repair for the measured path, not proof against every future
call-chain change. A fresh firmware build and device startup, allocation and
bounded configuration-save verification remain necessary before claiming the
installed failure is repaired. No unrelated endpoint, auth, persistence or
actuator behavior changes in this patch.

Verdict: PASS_WITH_ADVISORIES for the scoped code change. The two source comments were corrected to include already-counted HTTP frames;
retain the runtime acceptance limitations above.


## Implementation validation

Issue association: existing weather/playlist Dial work, issue11. The new host
regression failed against8192 with assertion/exit134, then passed with16384.
The complete `sh scripts/test_admin_dial.sh` suite passed, including auth/store,
production PBKDF2, rotation/input, HTTP routes, weather parser and worker tests.
An isolated ESP-IDF5.5.5 / Xtensa GCC14.2.0_20260121 build completed. Original
sdkconfig and dependency lock SHA256 values remained unchanged. This is not a
flash, runtime stack measurement, successful credential save or release claim.
