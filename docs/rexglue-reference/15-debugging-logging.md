# 15 - Debugging and logging

Scope: what logging and diagnostics exist at HEAD in this repository, how they are reached, what was temporary and was
removed, and what exists only on unmerged branches. Where Hard Corps has no reusable implementation it says so.
Related: [02-build-system.md](02-build-system.md) (build failures), [06-graphics-vulkan.md](06-graphics-vulkan.md),
[13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md),
[14-packaging-release.md](14-packaging-release.md) (what logs may never ship).

Tags and confidence follow the folder conventions (GENERIC_REXGLUE, REUSABLE_INFRASTRUCTURE, PLATFORM_WINDOWS,
PLATFORM_LINUX, GAME_ADAPTATION, HARD_CORPS_SPECIFIC; CONFIRMED / LIKELY / UNCERTAIN). No game or script was run while
writing this; statements come from source, history and the project's docs.

## 1. What exists at HEAD

| Facility | What it is | Where | Class | Validation |
| --- | --- | --- | --- | --- |
| ReXGlue logging macros | `REXLOG_INFO/WARN/ERROR` (app side, `rex/logging.h`), `REXGPU_WARN` inside the GPU plugin | all of `src/`, patched SDK files | GENERIC_REXGLUE (SDK API) | manual |
| Log file selection | When the "port folder" is not the executable folder (an AppImage), the app sets the SDK `log_file` cvar to `<portfolder>/logs/hard_corps_uprising_NNN.log` (next free number), falling back to `<user data>/logs`; otherwise the SDK's own naming in `logs` next to the program is used | `HardCorpsUprisingApp::OnConfigurePaths` in `src/hard_corps_uprising_app.h`, `hcu::NextLogFile` in `src/platform.cpp` | REUSABLE_INFRASTRUCTURE (needed because the AppImage mount is read-only) | manual; origin `9964f82` |
| Startup trail lines | `User data: ...` (one line each start; a warning when a migration fell back), `Player settings: <path>`, `Launcher shown`, `Window mode: borderless fullscreen / windowed`, `Scheduling: ...` (Windows), a warning when the window opened on another display than the setting, install and DLC progress/failure lines (`Installing game files from ...`, `DLC install failed: ... (detail)`) | `src/hard_corps_uprising_app.h`, `src/settings.cpp`, `src/launcher.cpp`, `src/window_host.cpp`, `src/platform.cpp` | REUSABLE_INFRASTRUCTURE | `tests/user_data_tests.cpp` checks the user-data log/About text; the rest manual |
| Warnings the player cannot see | offline stats corrupt or unwritable, achievement 0 not registerable, unsupported `default.xex` CRC32, settings repair | `src/live_fix.cpp`, `src/settings.cpp`, `src/launcher.cpp` | GAME_ADAPTATION / REUSABLE | `tests/live_tests.cpp` covers the stats store logic |
| Graphics start failure box (Linux) | `hcu::ReportGraphicsFailure()` shows an error window drawn in an SDL surface, not an SDL message box ("graphics system could not start ... needs a working Vulkan graphics driver ... the log folder next to the AppImage has the details") when `ReXApp::SetupPresentation` returns false; no-op on Windows | `SetupPresentation` override in `src/hard_corps_uprising_app.h`, `src/platform.cpp`; origin `6cf59bd` | PLATFORM_LINUX, REUSABLE_INFRASTRUCTURE | manual (not re-run) |
| GPU device log | `Vulkan device: <name> (type N)` when the Vulkan backend picks a device | patch 0026 ([01-rexglue-core.md](01-rexglue-core.md)) | LIKELY GENERIC_REXGLUE | manual |
| MSAA host-sample log | both render target caches log whether the game's 2x MSAA targets get host 2x or 4x and why | patch 0028 | the framing is HARD_CORPS_SPECIFIC; logging the host sample count is reusable | manual |
| On-screen performance | FPS in the window title (`hcu_show_fps`, default on) and an optional ImGui frame counter (`hcu_show_frame_counter`) | `src/frame_counter.cpp`, `src/window_host.cpp` | REUSABLE_INFRASTRUCTURE (the swap hook that feeds it is GAME_ADAPTATION, see [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md)) | `tests/frame_counter_tests.cpp` |
| ReXGlue overlays | `docs/launcher.md` refers to the SDK's F3 and F4 overlays (F4 is the settings overlay with "Save to config"); the F10 cheat menu deliberately avoids cvars because F4 would persist them | `docs/launcher.md`, `src/cheat_menu.cpp` | GENERIC_REXGLUE pitfall, section 8 | manual |
| Tracy | non-Release Windows builds stage `TracyClient<suffix>.dll` (the SDK profiler client) and `build.ps1` requires it | `tools/build.ps1`, `docs/first-boot.md` | GENERIC_REXGLUE | no use of Tracy is documented in this repo (UNCERTAIN) |
| Crash dumps | Windows: TRG's handler (`hcu::InstallCrashReports`, `src/platform.cpp`) writes `crash-<date-time>.txt` and a minidump into the logs folder; `*.dmp` stays git-ignored and a forbidden file type in packages | `.gitignore`, packagers | REUSABLE_INFRASTRUCTURE | `tests/lifecycle_tests.cpp` |

On Windows a crash leaves `crash-<date-time>.txt` and a minidump beside the logs (section 1); otherwise a crash is
diagnosed from the newest log plus the exit code (section 4).

## 2. Log files, locations and what to ask for

- Windows: `logs` next to the program (SDK naming such as `hard_corps_uprising_001.log`, which is also the name used by
  the release-scanner test case). Linux AppImage: `logs` next to the AppImage, `<user data>/logs` if not writable
  (`docs/linux.md`, `docs/user-data.md`).
- Logs are never part of a release and never inside the AppDir (forbidden `*.log` and `logs`,
  [14-packaging-release.md](14-packaging-release.md)).
- Bug-report request used in the release notes: attach the newest `hard_corps_uprising_*.log` and say what you were
  doing; for Linux also the distribution and graphics card.
- Log rotation: the SDK honors `log_max_file_size_mb` and `log_max_files` (CONFIRMED only because the branch-only
  telemetry config sets them, section 9); HEAD's shipped TOML does not set them.

## 3. Environment variables, CLI and cvars

| Name | Kind | Effect | Where |
| --- | --- | --- | --- |
| `APPIMAGE` | env (set by the AppImage runtime) | marks an AppImage run: port folder is the AppImage's folder; relaunch uses it | `src/platform.cpp` |
| `HCU_TRG_LAUNCHER_DIR` | env, build time | local TRG launcher checkout when the submodule is empty | `CMakeLists.txt` |
| `HCU_WORK` | env, build time | Linux packaging work dir | `tools/package-linux.sh` |
| `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT`, `__NV_PRIME_RENDER_OFFLOAD` | env | if one is set, patch 0026 does not override the player's Vulkan device choice | `docs/rexglue-runtime-patch.md` |
| `vulkan_device`, `d3d12_adapter` | SDK cvars | pick a device/adapter by index | same |
| `--game_data_root`, `--user_data_root`, `--cache_root` | CLI | path overrides; development builds pass them | `docs/user-data.md`, `tools/build.ps1 -Run` |
| `--hcu_skip_launcher=true` | CLI (internal) | passed by the launcher's restart action | `src/hard_corps_uprising_app.h` |
| `log_file` | SDK cvar | log path; set by the app for AppImage runs unless given | `OnConfigurePaths` |

Process rule for runtime tests in this project: use launcher settings, not hidden cvars or TOML edits, so a test
reflects what a player can do.

## 4. Startup and crash diagnosis

### Missing-function fatal

First boot ended with `[FATAL] Call to invalid or unregistered function at guest address 0x825C7ED0` and exit code
`0xC0000409` about 0.3 s after the guest thread started (`docs/first-boot.md`). Root cause was a ReXGlue v0.10.0
analysis gap: functions reachable only through vtables or data pointers, placed right after a `bctr` or an in-function
`b`. Process: hand overrides in `config/overrides.toml`, then the batch tool `tools/find-missing-functions.py` (report in
`build/missing-functions-report.txt`, HIGH-only apply, `--verify` after codegen). GAME_ADAPTATION for addresses; the
procedure (run the whole batch, do not iterate one fatal per runtime test) is transferable. Runtime skipping of
unregistered calls was rejected: the fatal comes from the prebuilt runtime and a skipped function could corrupt state.
CONFIRMED (docs); validation manual.

### Shutdown crashes and hangs

Signatures recorded in the repo: an Exit Game hang reported from SteamOS Game Mode (a destroyed window used while the
input driver detached; `418174b` detaches input first in `OnShutdown`) and a SIGSEGV "a few seconds after Execution
complete" under CPU load (a guest thread outliving runtime teardown; patch 0033, `abd31d2`). Automated coverage:
`tests/shutdown_order_tests.cpp` and `tests/title_thread_shutdown_tests.cpp`. The v0.5.0 notes say the reporter had not
confirmed the fix. See [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md).

## 5. Vulkan and graphics diagnostics

1. If the window never appears on Linux, the log says whether the SDK failed `SetupPresentation` and why (for example no
   Vulkan driver or loader). Origin `6cf59bd`.
2. Check the `Vulkan device:` line to confirm the intended GPU (patch 0026); on hybrid laptops use the variables in
   section 3.
3. The Linux brightness bug (patch 0034, SPIR-V exponent bias read from the wrong fetch constant word) was found by
   comparing with the DXBC translator, and no shader-dump tool exists in this repo. The shader cache is in
   `<user data>/cache`.
4. The per-frame Vulkan swapchain pipeline rebuild (patch 0035, upstream ReXGlue issue #507) appears as a GPU wait on
   every frame; no dedicated counter exists at HEAD.
5. Windows adapter choice follows the Windows graphics preference (patch 0027); no adapter log line is documented.

Renderer details: [06-graphics-vulkan.md](06-graphics-vulkan.md).

## 6. RenderDoc

Problem (CONFIRMED, `3020c32`, issue #6): `RenderDocAPI::CreateIfConnected` used `DynamicLibrary::Load`
(`dlopen("librenderdoc.so")` / `LoadLibraryW("renderdoc.dll")`), which loads RenderDoc whenever the loader can find it,
and loading RenderDoc in-process activates it. A SteamOS user got `RenderDoc API initialized` and the overlay without
having attached anything. Fix: patch 0010 adds `DynamicLibrary::LoadExisting` (`RTLD_NOLOAD` on POSIX,
`GetModuleHandleExW` with a counted reference on Windows) and `CreateIfConnected` uses it, so only an already attached
RenderDoc is used. Class: LIKELY GENERIC_REXGLUE (same SDK code in any project).

Validation: automated on Linux only. `tests/renderdoc_detect_tests.cpp` with `tests/fake_renderdoc.cpp` is run by
`build-patched-rexruntime.sh --loader-test` beside the patched runtime (must pass) and the stock runtime (must fail by
loading the fake library). The Windows half has no test. Packaging: the AppDir check rejects any file named like
RenderDoc or a Vulkan layer, JSON files, and binaries that link RenderDoc ([14-packaging-release.md](14-packaging-release.md)).

To capture frames, start the game under RenderDoc so the library is already loaded in the process; do not rely on it
being discoverable. The patch changes no GPU behavior and is not evidence about CPU or GPU load.

## 7. Performance diagnostics

| Aid | State | Notes |
| --- | --- | --- |
| FPS in window title, frame counter | production | launcher settings; fed by the swap hook |
| `DEBUG_SAULO` GPU thread statistics (timestamp queries, per-interval statistics, resolve signatures) | **removed** | a debug patch inside the numbered stack: `0006-debug-saulo-gpu-thread-stats` (`0ec7ded`), renumbered 0008 (`b681177`) and 0010 (`e950ca3`), deleted in `bbd02f2` ("remove debug instrumentation before release") |
| Tracy client | staged in non-Release Windows builds | usage undocumented here |
| Benchmark numbers | none recorded in the repository | treat any figure as unmeasured |

Lesson (CONFIRMED by history): keeping the debug patch inside the numbered stack renumbered a file that other docs
referenced every perf round, and changed the runtime fingerprint each time. If a temporary patch is unavoidable, give it
the highest number and a `debug` name, delete it in a dedicated commit, and never cite its number in permanent docs.
Whether a `0020` debug patch existed cannot be established from git: no commit touches that number.

## 8. DEBUG_SAULO conventions (temporary aids)

Conventions seen in history:

- Every temporary line carries `DEBUG_SAULO`: comment tag `// DEBUG_SAULO`, log prefix `[DEBUG_SAULO][<Tag>]`, files
  `src/debug_saulo*.{h,cpp}`, patches `NNNN-debug-saulo-*.patch`, and a header comment saying it is temporary and what to
  remove with it.
- Removal is a grep: `git grep -n DEBUG_SAULO HEAD` returns nothing (CONFIRMED). Run it before a commit meant to land and
  before a release. The tag is project-specific; the practice is reusable.
- Guest-function tracing wraps the original (`REX_EXTERN(__imp__sub_...)`, call the original, log registers) so behavior
  is unchanged. GAME_ADAPTATION technique.
- Do not keep obsolete debug code "for later". Every instance below was deleted; history still has it.

Removed instances:

| Item | Added | Removed | Notes |
| --- | --- | --- | --- |
| `src/debug_saulo_input.{h,cpp}` (keyboard/controller wrapper hook, `[DEBUG_SAULO][Input]` lines) | `1f2b0b2` | `8d6558b` | extra source in the executable target during input bring-up |
| GPU thread stats debug patch (D3D12 command processor and render target cache) | `0ec7ded` | `bbd02f2` | section 7 |

Pitfall: any debug toggle implemented as a cvar can leak into the player's TOML, because the SDK's F4 overlay "Save to
config" writes every modified cvar. The F10 cheat menu uses a session override layer instead
([16-reusable-game-features.md](16-reusable-game-features.md)).

## 9. Branch-only and experimental (not HEAD functionality)

These local branches are **not merged into HEAD** (`git branch --no-merged HEAD`). Nothing here is shipped or supported;
whether they compile or ran is not established by the repository.

| Branch | Commits | Content |
| --- | --- | --- |
| `debug/full-gameplay-telemetry` | `6806845`, `4a8f369`, `dd633c8` | `src/debug_saulo.{h,cpp}` (session header with build, config, backend, vsync, display, audio and input settings; an `F9` user marker that dumps the previous 15 s of buffered events; per-window series for frame interval, main-loop body, scene run, present call and GPU fence wait; window, DPI and focus monitor) and `src/debug_saulo_guest.{h,cpp}` (guest hooks logging player, damage, respawn, flow and render events; tags `SESSION`, `MARKER`, `CONTEXT`, `PERF`, `FLOW`, `PLAYER`, `DAMAGE`, `RESPAWN`, `RENDER`, `SNAPSHOT`); small hooks in `cheats.cpp`, `gpu_fence_wait.cpp`, `hard_corps_uprising_app.h`, `window_host.cpp`; its TOML sets `log_max_file_size_mb = 10` and `log_max_files = 500` for long sessions. HARD_CORPS_SPECIFIC / GAME_ADAPTATION. Reusable idea only: a marker key plus a ring buffer of recent events, so a tester can say "it happened just now" |
| `investigate/controller-rumble` | `66e229d` | `patches/rexglue-sdk/0036-debug-saulo-rumble.patch` (rumble logging in the input system, the XInput driver and `xam_input`) plus `src/debug_saulo_rumble.cpp` (a hook on a guest rumble setter, `sub_82668CB8`, logging motor and strength). It would change the runtime fingerprint if applied. HARD_CORPS_SPECIFIC |

## 10. Production versus temporary

| Item | Production | Temporary / removed | Branch-only |
| --- | --- | --- | --- |
| Launcher, startup and user-data log lines | yes | | |
| Install, DLC and settings-repair lines | yes | | |
| `Vulkan device:` line (0026), MSAA log (0028) | yes | | |
| FPS title and frame counter | yes | | |
| `ReportGraphicsFailure` error window (Linux) | yes | | |
| RenderDoc detection fix and test | yes | | |
| AppImage log placement (`NextLogFile`) | yes | | |
| GPU thread stats debug patch | | removed `bbd02f2` | |
| `debug_saulo_input` | | removed `8d6558b` | |
| Telemetry (F9 markers, guest hooks, perf windows) | | | `debug/full-gameplay-telemetry` |
| Rumble tracing patch 0036 and hook | | | `investigate/controller-rumble` |
| Crash dumps | not implemented | | |

## 11. How to verify (focused)

- Logging change: build RelWithDebInfo, start from the launcher, read the newest log for the expected line. For
  AppImage log placement, the fallback to `<user data>/logs` is the `OnConfigurePaths` loop.
- Temporary diagnostic added: `git grep -n DEBUG_SAULO` must show only intended lines, and be empty before it lands.
- RenderDoc: `tools/build-patched-rexruntime.sh --loader-test <sdk> <build>` on Linux (builds and runs the test target
  beside both runtimes).
- Do not use a Debug build to judge runtime-patch or performance behavior; it runs the stock runtime
  ([02-build-system.md](02-build-system.md)).

Full regression belongs to the release review only.

## 12. Origin commits

`9964f82` Linux log placement and AppImage id; `1f2b0b2` and `8d6558b` input debug wrapper; `0ec7ded`, `b681177`,
`e950ca3`, `bbd02f2` GPU stats debug patch lifecycle; `3020c32` RenderDoc fix and test; `abd31d2` and `418174b`
shutdown fixes; `6cf59bd` graphics failure box; `6806845`, `4a8f369`, `dd633c8` telemetry (branch only); `66e229d`
rumble tracing (branch only).
