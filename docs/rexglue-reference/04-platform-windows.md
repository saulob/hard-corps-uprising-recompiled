# 04 Windows platform

Win32 integration of the Hard Corps: Uprising port. Tags: `PLATFORM_WINDOWS` unless noted. Patch numbers refer to
`patches/rexglue-sdk`; the authoritative patch inventory is [01-rexglue-core.md](01-rexglue-core.md). Linux counterpart:
[05-platform-linux.md](05-platform-linux.md). Window and frame pacing behavior: [07](07-display-window-frame-pacing.md).

Source files: `src/platform.cpp` and `src/platform.h`, `src/window_host.cpp`, `src/gpu_fence_wait.cpp`,
`packaging/windows/hard_corps_uprising.rc`, `CMakeLists.txt`, `tools/build.ps1`, `tools/package-release.ps1`,
`tools/build-patched-rexruntime.ps1`, `tools/rexruntime-patch.ps1`.

## Summary table

| Topic | Implementation | Class | Confidence | Validation |
| --- | --- | --- | --- | --- |
| GUI subsystem, no console | `add_executable(hard_corps_uprising WIN32 ...)` | REUSABLE_INFRASTRUCTURE | CONFIRMED | manual (logs go to files) |
| Executable icon | `1 ICON "../icons/app-icon.ico"` in the `.rc` | REUSABLE_INFRASTRUCTURE | CONFIRMED | manual |
| Version-info resource, app manifest | none in the repo | - | CONFIRMED absent | n/a |
| Hybrid GPU hint | `NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance` exports in `platform.cpp` | REUSABLE_INFRASTRUCTURE | CONFIRMED (code) | pending on a hybrid machine |
| D3D12 adapter preference | patch 0027 | GENERIC_REXGLUE | CONFIRMED (code) | inferred; no hybrid-GPU test recorded |
| Monitors and DPI | `EnumDisplayMonitors` + `GetDpiForMonitor` | REUSABLE_INFRASTRUCTURE | CONFIRMED | manual |
| Pre-show window placement | `WH_CALLWNDPROC` hook | REUSABLE_INFRASTRUCTURE | CONFIRMED | automated for the planner logic only |
| Relaunch | `CreateProcessW` | REUSABLE_INFRASTRUCTURE | CONFIRMED | manual |
| High-resolution sleep | patch 0015 | GENERIC_REXGLUE | CONFIRMED (code) | no automated test; no number recorded for 0015 alone |
| Process timer and power throttling | `trg::TuneProcessScheduling()` | REUSABLE_INFRASTRUCTURE | CONFIRMED | manual (logged at start) |
| Fence wait on address | `WaitOnAddress` / `WakeByAddressAll` (0009 + `gpu_fence_wait.cpp`) | HARD_CORPS_SPECIFIC hook, GENERIC_REXGLUE wake side | CONFIRMED | manual |
| RTSS and other overlay compatibility | no investigation in the repo | - | UNCERTAIN | none |

## Executable and resources

- **Subsystem.** The program is linked with `WIN32` (no console window). There is no `AllocConsole` in `src/`.
  Diagnostics go to the ReXGlue log file ([15](15-debugging-logging.md)). A port that needs a console should run from a
  terminal with a build whose logging goes to stdout, or read the log file; this repo opens no console.
- **Icon.** `packaging/windows/hard_corps_uprising.rc` is one line, `1 ICON "../icons/app-icon.ico"`. CMake does
  `enable_language(RC)` and sets `OBJECT_DEPENDS` on the `.ico` so editing the icon rebuilds the resource. The comment
  in `CMakeLists.txt` gives the reason it is resource 1: Explorer, and SDL's window class, use the first icon of the
  executable, so `ApplyWindowIcon()` is an empty function on Windows. Origin `26bd37c`. PNG and ICO assets are generated
  from `packaging/icons/app-icon.svg` by `tools/make-app-icon.py`. Porting: copy directly.
- **No `VERSIONINFO` and no application manifest.** The `.rc` holds only the icon, so file-properties metadata is not
  set, and the repo declares neither DPI awareness, long-path support nor a UTF-8 code page. Which DPI mode the process
  runs in is decided at run time by SDL/ReXGlue and is not visible in the repo (UNCERTAIN). Paths are UTF-8 by
  convention instead: `hcu::Utf8` and `hcu::PathFromUtf8` in `platform.cpp` convert `std::filesystem::path` for TRG
  Launcher and the settings code.
- **Not code-signed.** `packaging/README.txt` warns players about SmartScreen.

## GPU selection on hybrid systems

Two independent mechanisms for machines with an integrated and a discrete GPU.

1. **Exported driver hints** (`platform.cpp`, Windows only): `extern "C" __declspec(dllexport) DWORD
   NvOptimusEnablement = 1` and `int AmdPowerXpressRequestHighPerformance = 1`. The NVIDIA Optimus and AMD switchable
   graphics drivers run a program exporting these on the discrete GPU. They live in the executable, which is where the
   drivers look. Listed in the v0.5.0 release notes.
2. **SDK patch 0027** (`0027-d3d12-adapter-preference.windows.patch`, `src/ui/d3d12/d3d12_provider.cpp`;
   GENERIC_REXGLUE; CONFIRMED in code). Stock ReXGlue v0.10.0 took the first adapter DXGI enumerated, which is the
   integrated GPU on many laptops. The patch enumerates with `IDXGIFactory6::EnumAdapterByGpuPreference` and
   `DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`, or `MINIMUM_POWER` when the player set "Power saving" for this executable in
   Windows graphics settings. It reads that choice itself: `GetUserGpuPreference()` queries
   `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`, value named by the full executable path, text containing
   `GpuPreference=1` (power saving) or `2` (high performance). It applies only while the cvar `d3d12_adapter` is `-1`;
   an explicit index still wins. The patch adds `#pragma comment(lib, "advapi32.lib")` for `RegGetValueW`. When the
   factory does not support `IDXGIFactory6` it falls back to `EnumAdapters1`.
   Pitfall: the registry value is keyed by executable path, so a moved portable folder loses the player's Windows
   setting. Verify: read the adapter line in the log on a machine with two GPUs, then flip the Windows setting.
   Validation: none recorded for a hybrid machine. The only run evidence is single-GPU (`docs/first-boot.md`: the
   RTX 4070 was selected, before 0027 existed). Treat as pending. Origin `cf6eee1`. Vulkan twin 0026:
   [06](06-graphics-vulkan.md).

Porting: the exports are copy-directly. 0027 is likely relevant to any D3D12 ReXGlue project; re-check the hunk against
the SDK revision you pin.

## Monitors, DPI and window placement

All in `platform.cpp`; the launcher side is [03](03-launcher.md) and `docs/launcher.md`.

- **Monitor list.** `ListMonitors()` uses `EnumDisplayMonitors`, `GetMonitorInfoW` (`rcMonitor`, `rcWork`) and
  `GetDpiForMonitor(MDT_EFFECTIVE_DPI)`; `scale = dpi / 96`. It needs the `shcore` import library (`CMakeLists.txt`
  links `shcore` and `synchronization` on Windows). The primary monitor is moved first with `std::stable_partition`
  because ReXGlue's `monitor = N` opens the window on `SDL_GetDisplays()[N-1]` and SDL lists the primary first.
  `docs/launcher.md` records that the order was checked against SDL 3.4.10 source while the SDK ships SDL 3.5.0, whose
  source was not on disk (UNCERTAIN for 3.5.0). `CheckWindowDisplay()` logs a warning when the window opened on a
  different display than the setting names (Windows only; a no-op on Linux).
- **DPI scaling.** `window_width` and `window_height` are logical pixels that ReXGlue multiplies by the display scale:
  `docs/first-boot.md` notes 853x480 became about 2880x1620 physical at roughly 337% scaling. The launcher hides
  window sizes that cannot fit the display at its scale. Fonts: `OnConfigureFonts` passes
  `window()->GetDpi() / window()->GetMediumDpi()` to `LoadLauncherFonts`.
- **Placement before first show.** `InstallPreShowPlacement` installs `SetWindowsHookExW(WH_CALLWNDPROC)` on the
  creating thread. On the first top-level `WM_SHOWWINDOW` (`wParam` true, `lParam == 0`, no parent) it calls
  `SetWindowPos` and optionally `SW_MAXIMIZE`, then unhooks. `TakePreShowResult()` unhooks too if the window never
  showed. It avoids a visible jump because ReXGlue 0.10.0 cannot move or resize an open window. On Linux it is empty and
  the launcher places the window after it exists.
- **Query and apply.** `QueryWindowState` uses `GetWindowRect`, `IsIconic`, `IsZoomed` and `GetWindowPlacement`
  (`WPF_RESTORETOMAXIMIZED` when minimized); `ApplyWindowPlacement` does `SW_RESTORE`, `SetWindowPos`, `SW_MAXIMIZE`.
  Origin `e064dd9`. Automated: `tests/window_placement_tests.cpp` covers the planner in `src/window_placement.h`
  without a window; the Win32 calls are manual.
- **Relaunch.** `RelaunchSelf` calls `CreateProcessW` with `GetCommandLineW()` plus extra arguments
  (`--hcu_skip_launcher=true`), closes both handles, and the caller quits through `QuitFromUIThread()`. A relaunch is
  how a changed window size, monitor or VSync is applied. The Linux twin uses `posix_spawn`.
- **F11 and cursor.** `src/window_host.cpp` registers a window listener and an input listener at z-order 1000 so F11
  never reaches the game; fullscreen is SDL's borderless desktop mode. `CursorHider` is tested by
  `tests/window_host_tests.cpp`. These parts are SDL level and behave the same on Linux ([07](07-display-window-frame-pacing.md)).

## Input at the launcher level

- **Shift at startup.** `trg::ShiftHeld()` is `GetAsyncKeyState(VK_SHIFT)`, valid before any window exists.
  `ShiftHeldAfterWindowOpened()` returns `false` on Windows; on Linux it re-checks once the window has focus
  ([05](05-platform-linux.md)).
- **Launcher controller.** `ImGuiGamepad` (`src/imgui_gamepad.*`) uses SDL3 gamepads rather than XInput, so any pad SDL
  recognizes works and the Steam Controller is no longer invisible. On Windows the exe links its own static SDL (the
  runtime DLL embeds another copy), so this is a second, independent SDL instance with its own event queue, drained
  with `SDL_PollEvent`. It exists because ReXGlue's input system starts only with the game. Game input is ReXGlue's
  ([08](08-input-gamepad-mouse.md)).
- **Fonts.** `FindUiFonts()` uses Segoe UI (`segoeui.ttf`, `seguisb.ttf`, `segoeuib.ttf`) from `GetWindowsDirectoryW`.
- **Folders and dialogs.** TRG's `OpenInFileManager` uses `explorer.exe /select` or `ShellExecuteW`; its file dialog
  loads `comdlg32` on first use.

## Timers and scheduling

- **Patch 0015** (`0015-windows-high-resolution-sleep.windows.patch`, `src/core/threading_win.cpp`; GENERIC_REXGLUE for
  Windows hosts; CONFIRMED in code). Problem: guest `Sleep` and alertable sleep lasted up to a timer tick longer than
  asked; `docs/rexglue-runtime-patch.md` records a 1 ms sleep taking 1.8 ms on the test machine. Fix: a thread-local
  `SleepTimer` wrapping `CreateWaitableTimerExW(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`, used from 1 ms up, with
  `WAIT_IO_COMPLETION` mapped to `SleepResult::kAlerted`; below 100 us it yields; if the timer cannot be created it falls
  back to `::Sleep` / `SleepEx`. Waits with a timeout (`WaitForSingleObject` and friends) still follow the system
  timer. The high-resolution flag needs Windows 10 1803 or newer. Linux does not need it (`nanosleep`). Origin: first
  part of 0006 in `b681177`, split into its own patch in `dc6feb8`. Verify: log a 1 ms sleep before and after.
  Validation: inferred from that measurement; no automated test.
- **`TuneProcessScheduling()`** (TRG Launcher `game_helpers`, called in `OnPostSetup`, Windows only): `timeBeginPeriod(1)`
  held until exit, plus an opt-out of Windows 11 power throttling (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED` and
  `IGNORE_TIMER_RESOLUTION`). It logs `Scheduling: 1 ms sleep took X ms, now Y ms (...)`. Reusable as is.
- **Vblank thread priority** (inside 0006) is one level above normal on Windows only.

## Waiting on a memory word

The game's GPU-fence wait loop spins; the port makes it block. `src/gpu_fence_wait.cpp` hooks the begin and poll helpers
of that loop and waits on the fence word with `WaitOnAddress` (2 ms upper bound; the Linux branch uses `futex`), and
patch 0009 makes the command processor call `WakeByAddressAll` after its guest memory writes. `CMakeLists.txt` links
`synchronization`. The hook addresses are `HARD_CORPS_SPECIFIC`; the wake side and the pattern are reusable. See
[12](12-performance.md) and [13](13-crash-hardening-threading-memory.md).

## Paths and user data

`PortFolder()` is `rex::filesystem::GetExecutableFolder()` on Windows. `game`, `logs` and `user_data` sit next to the
executable; `--game_data_root` and `--user_data_root` override. Builds before v0.3.2 kept data in
`Documents\hard_corps_uprising`; `SelectUserData` (`src/user_data.cpp`) moves it in (rename, else verified copy). If
`user_data` cannot be written, for example under `Program Files`, the system folder is used for that run and a warning
is logged, so the readme tells users not to install there. The log-file override in `OnConfigurePaths` applies only when
the port folder differs from the executable folder (the AppImage case), so on Windows ReXGlue's default log location is
used. See [10](10-user-data-settings-migration.md).

## Dependencies and DLL staging

- **Files next to the executable:** `rexruntime.dll` and `rexgpu-xenos.dll` (Release); `rexruntimerd.dll`,
  `rexgpu-xenosrd.dll` and `TracyClient*.dll` (RelWithDebInfo); the `d` variants (Debug); and `hard_corps_uprising.toml`,
  copied by a post-build step. The Xenos GPU plugin is a separate DLL loaded by name from the executable folder
  (`gpu_plugin = "xenos"`); a load failure is fatal. The release ships no separate SDL DLL; SDL is presumably inside
  the runtime (LIKELY, not asserted by the repo).
- **Stale DLL prevention** (REUSABLE_INFRASTRUCTURE; a generic lesson for any patched-runtime project). The SDK copies
  its DLLs next to the program only when the program links. `CMakeLists.txt` therefore adds each SDK DLL to
  `LINK_DEPENDS`, and the targets `hcu_rexglue_check` and `hcu_rexglue_staging` run
  `build-patched-rexruntime.ps1 -Check` and `-Staged` on every build, including preset and IDE builds. `tools/build.ps1`
  rebuilds the patched runtime when its stamp is stale and fails unless the staged DLLs are byte-identical to the
  installed ones; `tools/package-release.ps1` re-checks hashes before zipping. Fingerprint design:
  [02](02-build-system.md) and `docs/rexglue-runtime-patch.md` ("What the build verifies"). The CMake check calls
  Windows PowerShell by fixed path with `PSModulePath` unset, because a Store `pwsh` or an inherited module path breaks
  it.
- **Release ZIP** (`$RootFiles` in `package-release.ps1`): the executable, the two DLLs, the toml, `README.txt`,
  `LICENSE` and a `licenses` folder. The release check rejects game files, packages, saves, logs and any file containing
  the repository folder path (ASCII or UTF-16).
- **User requirement:** the Visual C++ 2015-2022 x64 redistributable (`packaging/README.txt`).
- **Toolchain:** the `windows-base` preset uses Ninja with `clang` and `clang++`; ThinLTO with lld-link in Release and
  RelWithDebInfo ([02](02-build-system.md)).

## Startup and shutdown

- Startup order is ReXApp's (`OnConfigurePaths`, `OnPreSetup`, `SetupPresentation`, `OnFinalizePaths`, `OnPostSetup`);
  `TuneProcessScheduling` and `AttachWindowHost` run in `OnPostSetup`.
- Shutdown: `OnShutdown` calls the input system's `Shutdown()` first because ReXApp destroys the window before the
  runtime, and the keyboard and mouse driver touches the window when it detaches (`418174b`). `tests/shutdown_order_tests.cpp`
  runs the real UI loop and the teardown order, cross-platform; the real Windows Exit Game path is a manual check.
  Guest-thread stopping (patch 0033, `abd31d2`) is in [13](13-crash-hardening-threading-memory.md).
- TRG Launcher offers `InstallCrashReports` (Windows `SetUnhandledExceptionFilter` writing to the logs folder). This
  port does not call it (no match in `src/`).

## Overlays and external hooks (RTSS and similar)

The repository, its history and all local branches contain no RTSS, RivaTuner, MSI Afterburner, ReShade, Steam overlay
or Discord overlay investigation (searched with `git log -S` and `git grep` across all branches; the only
"afterburner" hit is an unrelated reference project name in `docs/package-analysis.md`). Status: UNCERTAIN, nothing
tested. The one overlay incident is RenderDoc being activated on SteamOS (issue #6, patch 0010,
[05](05-platform-linux.md)); 0010 also changes the Windows lookup (`GetModuleHandleExW`) but its test is Linux only.
The port's own overlays (achievements, cheat menu, frame counter) are ImGui dialogs drawn by the presenter, not injected
hooks. Patch 0029 changes how often UI repaints present ([07](07-display-window-frame-pacing.md)); no interaction with
presenter hooks was reported. A project that needs overlay compatibility should test it fresh.

## Pitfalls

- Moving a portable install changes the executable path and drops the Windows per-app GPU preference 0027 reads.
- Without per-monitor DPI awareness `GetDpiForMonitor` may report virtualized values; which mode applies is UNCERTAIN.
- Windows-only patches (0011-0015, 0023, 0027, 0031) must keep the `.windows.patch` suffix; the tooling rejects names
  outside the convention and keeps the Linux fingerprint independent.
- The build checks can trigger a runtime rebuild; they are not safe "dry runs".

## How to verify (focused)

1. Icon in Explorer and taskbar; no console window.
2. Log line `Scheduling: ...` is present.
3. The window opens on the monitor the setting names (no display-mismatch warning) at 100% and at a scaled display.
4. F11 toggles; the cursor hides after 5 seconds in fullscreen only.
5. Two-GPU machine: the log shows the discrete adapter, and the integrated one after choosing Power saving in Windows.
6. `tools\build.ps1` reports the staged DLLs match `tools\rexglue`.

Origin commits: `26bd37c` (icon), `e064dd9` (placement, cursor), `8d6558b` (F11), `27e26ea` (portable data), `dc6feb8`
(0015), `cf6eee1` (0026-0029), `418174b` (input shutdown), `b31b959` (Windows packaging).
