# 03 Launcher

Scope: the pre-game launcher of Hard Corps: Uprising Recompiled, how it is built into a ReXGlue application, how it
installs and validates game data from an Xbox 360 package, and what another project can reuse. Paths are
repo-relative. Settings storage and migration are in [10-user-data-settings-migration.md](10-user-data-settings-migration.md);
the achievements the launcher lists are in [11-offline-services-profile-achievements.md](11-offline-services-profile-achievements.md);
the SDK patches are inventoried in [01-rexglue-core.md](01-rexglue-core.md). Existing project docs with the same
material from the player's side: `docs/launcher.md`, `docs/display.md`, `docs/linux.md`.

Tags: `REUSABLE_INFRASTRUCTURE` unless an item says otherwise. Confidence is CONFIRMED when the repo has the code and
either a named automated test or documented manual verification. "Manual" means repo documentation or release notes
say it was exercised, not that this reference re-ran it.

## 1. Architecture

**What it is.** The launcher is the third-party TRG Launcher library (`third_party/trg-launcher`, a git submodule,
pinned in `docs/launcher.md` to `b384a28`, "TRG Launcher 1.1.0") drawn inside ReXGlue's own Dear ImGui frame, in the
game window. There is no second process, window, SDL instance or ImGui instance (CONFIRMED, `src/launcher.cpp`,
`CMakeLists.txt`, `docs/launcher.md`). The Hard Corps layer is `src/launcher.cpp` (`LauncherDialog`, a
`rex::ui::ImGuiDialog`), `src/settings.cpp` (`GameSettings`, a `trg::SettingsStore`), `src/platform.cpp`
(OS helpers) and the glue in `src/hard_corps_uprising_app.h`.

- TRG supplies the page frame, theme, header, settings widgets (`Toggle`, `Choice`, `Combo`, `Segmented`),
  `Task` (background job with progress and Cancel), `BrowseForFile`, `OpenInFileManager`, `TextFileSettings` and
  the achievement grid. The project supplies pages, options, install logic, window logic, theme colors.
- ImGui is the copy inside `rexruntime`; CMake passes it to TRG as `TRG_LAUNCHER_IMGUI_TARGET` with the standalone,
  fetch-deps and demo options off (`docs/launcher.md`). Fonts are system fonts (`FindUiFonts` in `src/platform.cpp`:
  Segoe UI on Windows, a fixed family list on Linux), loaded through the app's `OnConfigureFonts`. No font file is
  bundled.
- Upstream licensing: `docs/launcher.md` records that the TRG repository has no LICENSE file. A project reusing TRG
  has to settle that independently (UNCERTAIN for any other project).

**Lifecycle hooks used (GENERIC_REXGLUE API, REUSABLE pattern).** `ReXApp` offers `OnConfigurePaths`,
`OnPostInitLogging`, `OnConfigureFonts`, `OnPreSetup`, `SetupPresentation`, `OnFinalizePaths`, `OnPostSetup`,
`OnShutdown`. The launcher relies on one capability: `OnFinalizePaths(defaults, resume)` may return `std::nullopt` and
call `resume(paths)` later, which defers building the runtime (the guest) until the player presses Play. The window,
ImGui drawer and the GPU plugin already exist at that point; the guest does not. Sequence in this repo:

1. `OnConfigurePaths`: game folder default (`<port folder>/game`), user data selection, log file for AppImages.
2. `OnPostInitLogging`: log the user-data decision, then `LoadPlayerSettings` loads `<user data>/settings.toml` over
   the shipped `hard_corps_uprising.toml` (see [10](10-user-data-settings-migration.md)).
3. `OnPreSetup`: `GameSettings::Capture` records the settings in effect; `ShouldShowLauncher` decides; if shown,
   `PrepareLauncherWindow` overrides the window cvars (windowed, launcher size, monitor 0); then
   `MarkWindowCreated` records the window parameters this process creates its window with.
4. `SetupPresentation` creates the window and loads the GPU plugin, which registers the Graphics cvars. If it fails
   the app opens `RunLauncherWithoutGraphics` (Linux): a launcher window on an SDL surface that shows the game files and
   graphics status, and Play reports the graphics failure with `ReportGraphicsFailure` (section 14).
5. `OnFinalizePaths`: sets title and icon, `CaptureLateRegistered` (Graphics keys registered only now), re-checks
   Shift on Linux, then either returns the defaults (no launcher) or calls `ShowLauncher` and returns `nullopt`.
6. Play: `callbacks.play` queues, on the UI thread, `GameSettings::Apply()` then `resume(defaults)`.

Porting: copy the lifecycle idea and the two-phase settings capture. Adapt the page list. Concept only for TRG itself:
any ImGui dialog on `rex::ui::ImGuiDrawer` works, TRG is a convenience (and a third-party license question).

## 2. Startup decision and first run

`ShouldShowLauncher` / `LauncherWanted` (`src/launcher.h`, `src/launcher.cpp`): the launcher is shown when game files
are not ready, whatever the setting says; otherwise when Shift is held or `hcu_launcher` is true, unless the one-shot
`hcu_skip_launcher` flag was passed. `hcu_skip_launcher` is read and then reset to default in the same call so it is
never persisted (CONFIRMED, code). Pitfall avoided: letting a "do not show the launcher" preference hide the only way to
install the game. `LauncherWanted` is a pure inline function so it is unit-testable; `installer_tests.cpp`
(`TestLauncherDecision`) covers the decision (automated, `hcu_installer_tests`).

Shift detection: Windows uses TRG `ShiftHeld` before the window exists; Linux SDL cannot see the keyboard before a
window has focus, so `ShiftHeldAfterWindowOpened` re-checks in `OnFinalizePaths`. Documented limitation in
`docs/linux.md`: Shift must stay held until the window appears (PLATFORM_LINUX, manual).

First run: there is no separate first-run wizard. A first start has no `game` folder, so `CheckGameFiles` returns
missing, the launcher shows with the Play page on a "Game files needed" card and an accent "Install from package..."
button. `user_data` is created on demand; `settings.toml` is created with a two-line comment header on the first save
(`OpenPlayerFile` in `src/settings.cpp`). Play is disabled until the files are ready (`can_play` in `MakeConfig`,
and `RequestPlay` re-checks).

## 3. Game data: validation, fingerprint, install (GAME_ADAPTATION, HARD_CORPS_SPECIFIC values)

Everything in this section is about this title's data. The mechanism (stage, verify, swap, roll back) is reusable; the
constants and the package format are not.

### 3.1 One readiness check

`CheckGameFiles(game_dir)` in `src/game_files.cpp` is the single source of truth, used by the Play page, the
install verification and the launcher-visibility decision. Ready requires `default.xex` present, a `DataXENON`
directory, file size equal to `kXexSize` (12,955,648) and CRC32 equal to `kXexCrc32` (0x3DF903B9) in
`src/game_files.h`. Results are three states: ready, missing, unsupported (wrong size or CRC). The CRC is computed
with a streaming 1 MiB chunk loop and is the "fingerprint" of the supported build; it is the same pin that the
codegen step uses (`tools/codegen.ps1`, see [02-build-system.md](02-build-system.md)). Rationale: static recompilation
is bound to one binary, so any other build must be refused before the runtime crashes on missing functions.
Confidence CONFIRMED; automated: `TestInvalidXex`, `TestValidInstallAndReinstall` in `tests/installer_tests.cpp`.

Porting: copy the structure (one function, three states, size plus CRC32 of the executable). Replace the constants and
the required-folder check. Pitfall: a size-only check is not enough when two builds share a size.

### 3.2 Install from package (`InstallFromPackage`)

Runs on a TRG `Task` so the UI stays responsive; hooks report phase and progress and return false to cancel.

1. Resolve the target and its parent; refuse when no usable parent exists (`kNoGameFolder`).
2. Open the package and read the STFS header: title ID must equal `kTitleId` (58410A18), content type must be the XBLA
   game type `000D0000` (so a DLC package is rejected with a specific message). Then load and validate the file table.
3. If a target already exists it must be "replaceable": empty, or containing `default.xex` or `DataXENON`. A folder
   with other content is refused (`kForeignFolder`) and never replaced. (Evidence: `IsReplaceable`;
   `TestForeignFolder`.)
4. Clear any stale staging folder, check free space against package size plus a 64 MiB margin (`kNoSpace`).
5. Extract into `game-install-temp` next to the game folder (same volume so the final step is a rename). A scope guard
   deletes the staging folder on any failure or cancellation.
6. Verify the staged folder with `CheckGameFiles`; wrong CRC gives `kWrongVersion`, missing files `kIncomplete`.
7. Final cancel check, then swap: existing `game` is renamed to `game-install-backup`, staging is renamed to `game`;
   if the second rename fails the backup is renamed back. Only after success is the backup removed.
8. Renames go through `RenameWithRetry`: up to 10 attempts, 300 ms apart, because a virus scanner can hold a freshly
   written folder. This is a Windows-motivated defence; it is harmless elsewhere.

Properties: existing game untouched on every failure path before the swap; the swap is two renames with a restore on
the failure of the second. A crash between the two renames would leave `game-install-backup` and no `game` (UNCERTAIN
whether any recovery on next start exists: no code looks for the backup folder; LIKELY not handled). The install
messages the player sees come from `DescribeInstall(status)`; the log gets the technical `detail`.

Validation: automated, `hcu_installer_tests` builds synthetic STFS packages (including a `default.xex` of the pinned
size and CRC generated from bytes) so no game files are needed; it also supports `--package <file> --reference
<folder>` to compare a real extraction. `TestOnWorkerThread` runs the install through a worker thread. Manual: release
notes v0.1.0 describe the Title ID and version checks. CONFIRMED.

### 3.3 STFS package reader (`src/stfs.cpp`) (GAME_ADAPTATION, conditional)

Treat the package format as an assumption of an XBLA title. The reader is read-only STFS (`LIVE`, `PIRS`, `CON `) with
consecutive-block files, which is what XBLA packages use; fragmented files report `kUnsupported`. It reads the content
and title IDs, content ID, English display name and license entries from the header. It computes block offsets with the
hash-table interleave (one table per 0xAA blocks and two more levels). It does not verify block hashes or the Microsoft
signature. Safety:

- Every entry name must be a single printable-ASCII component with no `..`, separators, drive or stream marker
  characters, no reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9), no trailing dot or space; parents must be
  directories; nesting limited (32) and entries limited (0x40000). Extraction also checks each destination stays
  inside the root. Tests: `TestUnsafeNames`, `TestUnsafeStructure`, `TestExtractionStaysInside`,
  `TestBlockBoundary`, `TestTruncatedAndPartial`, `TestUnsupportedLayouts`.
- Linux fix (`9964f82`): `Open` rejects non-regular files first, because on Linux an `ifstream` can open a directory
  or pipe. (PLATFORM_LINUX, CONFIRMED in the commit.)

Porting: if the target title ships as another container (GOD, disc ISO/XISO, loose files, ZAR), this file is a starting
point for the validation and staging pattern only. Inspect-first: block-per-level constants, header offsets, `0x40`
consecutive flag. The loose-file case (user supplies an extracted folder) needs only section 3.1 plus a copy.

### 3.4 DLC install, uninstall and listing (HARD_CORPS_SPECIFIC content, REUSABLE mechanism)

Evolution: `f66eeca` added install and removal; `d1f1a19` ("finalize DLC support review") tightened the safety checks.
Facts verified in `src/game_files.cpp`:

- Accepts title 58410A18 and content type `00000002` (marketplace content); rejects game packages.
- Installed under the SDK's marketplace layout in the user data root: `<user data>/0000000000000000/58410A18/00000002/<content ID>/`
  with a `Headers/00000002/<content ID>.header` beside it. The content ID is the 40-hex ID from the package, not the file
  name. The header is a 0x148-byte record the SDK content manager reads, plus the license mask appended when the
  package has one (`WriteContentHeader`). The layout is the ReXGlue content layout for this SDK version (see
  [01-rexglue-core.md](01-rexglue-core.md) and the DLC finding in project memory: the base game already has the DLC
  character assets; the marketplace item is mounted by the game). Inspect-first for any other title.
- Staging in `<user data>/dlc-install-temp/{staged,backup}`; both the content folder and the header are swapped with
  `SwapIn`, which keeps the previous copy in `backup`; failure of the header swap calls `RevertSwap` on the folder.
  A previous copy that cannot be put back is kept and named in the log detail (`kStranded`).
- Reparse-point defence: before touching anything the code requires the work folder, the installed folder and the
  header to be plain (no junction, symlink or other reparse point at or under them), via `HasReparsePoint`
  (`GetFileAttributes` on Windows, `is_symlink` elsewhere) and `IsSafeWorkFolder` (only exactly
  `<root>/dlc-install-temp` or `<root>/dlc-remove-temp`). Mismatch gives `kUnsafeTarget`. The reason is that a recursive delete (or a rename-based swap) through a junction or symlink could reach outside the work area (inferred rationale from `ContainsReparsePoint`; the code comment says recursive deletion must not follow a link).
- Removal (`RemoveInstalledDlc`): only a "verified" package (header names this title, content type and the same
  content ID) whose folder is a plain direct child of the DLC storage and that is not or does not contain the game
  folder. The folder is moved aside first, the header deleted, and if the header deletion fails the folder is moved back
  and the result is a failure. Tests: `TestUninstallEachCharacter`, `TestUninstallKeepsUserData`,
  `TestUninstallRejectsBadTargets`, `TestFailedUninstallIsNotSuccess`, `TestDlcFailuresKeepInstalled`,
  `TestDlcUnsafeEntries`, `TestDlcInstallSideBySide`.
- Classification: three known content IDs (`kSupportedDlc`) map to the three characters; otherwise the English display
  name (trimmed, case-folded, with optional "Additional Character" suffix). Anything else shows under "Other installed
  content" and is still removable. The UI state is derived from disk each time, never stored.

Porting: the SwapIn/RevertSwap/guard pattern and the reparse-point rules are COPY-OR-ADAPT. Content IDs, names and the
three-character cards are HARD_CORPS_SPECIFIC.

## 4. File chooser and fallbacks

The Hard Corps app has no file-chooser code of its own; it calls `trg::BrowseForFile(title, filters)` in
`BrowseAndInstall` and `BrowseAndInstallDlc` (`src/launcher.cpp`). The behavior comes from
`third_party/trg-launcher/src/platform.cpp` (third-party, read only here):

- Windows: `GetOpenFileNameW` loaded dynamically from `comdlg32.dll`, owner `GetActiveWindow()`, flags
  `OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR`. The project filter is `*` ("Xbox Live Arcade package" /
  "Xbox 360 package") because Xbox 360 packages have no required extension.
- Linux: runs `zenity --file-selection` first; if zenity is not installed (`RunDialog` detects exit 127) it runs
  `kdialog --getopenfilename`. Both through `popen`. `docs/linux.md` lists zenity or kdialog as a requirement.
- There is no third fallback and no in-app path-entry box. If neither utility exists the call returns an empty string
  and the launcher does nothing: no message is shown (inferred from code; the Hard Corps code treats empty as cancel).
  Also, `popen` blocks the calling thread while the dialog is open, so the ImGui frame stops (inferred; not mentioned
  in repo docs).
- Drag and drop of a package is listed as out of scope in `docs/launcher.md`.

Porting (adapt): if the target must work on minimal Linux (Steam Deck game mode, containers), add an in-ImGui path
entry or a portal call and show a message when no chooser exists. That improvement is not implemented here; do not
describe it as existing.

## 5. Pages, settings UI and the Save / Play model

Pages in order (`MakeConfig`): Play, DLC, Display, Graphics, Gameplay, Cheats, Achievements, Controls, About. The
option-to-cvar table is in `docs/launcher.md`; only the generic design is repeated here.

- Settings are a `trg::SettingsStore` subclass (`GameSettings`) holding `saved_`, `pending_` and `window_created_`
  maps. Edits stay pending. `Save()` writes only keys whose pending value differs from saved, refuses a key whose
  cvar is not registered (never writes a key the runtime would ignore), and writes through `trg::TextFileSettings` so
  comments and other lines survive. `SaveNow(values)` writes values immediately for window placement and does not touch
  pending edits. `ResetToDefaults()` re-reads the shipped TOML (`toml++`) and falls back to the cvar default.
  `Apply()` copies pending values into the running cvars, except window keys (they are fixed at window creation).
- The key whitelist is `kGameSettingKeys` in `src/settings.cpp`. Adding an option means: add the key, define the
  cvar if it is project-owned, add a UI row. A key in neither place is neither saved nor reset. CONFIRMED.
- Two-phase capture: `Capture` in `OnPreSetup`, `CaptureLateRegistered` in `OnFinalizePaths`, because GPU-plugin cvars
  (render resolution, post effect, anisotropy) do not exist until `SetupPresentation`. Without it those controls would show
  empty or default values. A key the player already edited is never overwritten.
- Play (`RequestPlay`) is the only save in the Play path: if `Save()` fails the launcher stays open with an error;
  otherwise it shows "Settings saved. Starting game..." for 0.4 s (timed by ImGui time, no sleep), disables the pages and
  clears input each frame so Play, Save, Ctrl+S, Esc and the D-pad cannot act twice, then `StartGame`.
- `ShouldShowLauncher`-related setting `hcu_launcher` is exposed on the Play page as the "Show this launcher" row.
- Reset all settings is on About; it only changes pending values.
- Project-owned cvars use the `hcu_` prefix, category "Hard Corps", registered with `REXCVAR_DEFINE_*`
  (`src/settings.cpp`). That keeps them in the same config machinery as SDK cvars.
- Settings migration lesson from `cf6eee1`: the Multisampling UI originally wrote the SDK cvar `native_2x_msaa`
  directly; it was replaced by a project cvar `hcu_msaa_compatibility`, and `ApplyMultisampling` sets
  `native_2x_msaa` from it at every start, which also overwrites a stale `native_2x_msaa = false` that older launchers
  wrote. See [10](10-user-data-settings-migration.md) section on settings evolution.

## 6. Display, window and the launcher/game window separation

This is the most project-specific lesson in the launcher and it is caused by an SDK limitation recorded in
`docs/launcher.md`: ReXGlue 0.10.0 has no API to resize or move an open window, so size, monitor and VSync are fixed
when the window is created. (GENERIC_REXGLUE limitation, CONFIRMED by the doc; details of the SDK API are in
[07-display-window-frame-pacing.md](07-display-window-frame-pacing.md).)

- Launcher window: `PrepareLauncherWindow` sets cvars `fullscreen=false`, `window_width/height` to 1280x720 logical capped
  to 90% of the primary work area (`LauncherWindowSize`), and `monitor=0` so SDL centres on the primary display.
- Game window: created with the saved Window size, Monitor, VSync and Display mode.
- `GameSettings::WindowChanged()` compares pending `window_width`, `window_height`, `monitor` and the tearing key
  (`kTearingSetting`: `d3d12_allow_variable_refresh_rate_and_tearing` on Windows,
  `vulkan_allow_present_mode_immediate` on Linux) with the values this process created its window with. `monitor` 0 and
  1 are normalised to the same value (`WindowValue`).
- If they differ at Play, `StartGame` calls `callbacks.restart`, which `RelaunchSelf("--hcu_skip_launcher=true")`
  and quits this process. The copy has no launcher, so it creates its window natively with the saved settings and cannot
  loop. If relaunch fails the launcher stays open with a message and "Press Play to try again". With the shipped defaults
  (853x480 game vs 1280x720 launcher) a normal Play relaunches once. Documented as expected.
- Otherwise Display mode (fullscreen toggle) and non-window settings apply in place. The launcher's own window
  placement is restored (`RestoreStartupWindow`) so the game gets the window it was created with.
- Window size list: fixed list (`kWindowSizes`: 853x480, 1280x720, 1600x900, 1920x1080) filtered by what fits the selected
  monitor at its display scale; the current value is always listed. Labels show on-screen pixels when the scale is not
  1. Monitor list comes from `ListMonitors()` in SDL display order, which is the order ReXGlue's `monitor = N` uses
  (`SDL_GetDisplays()[N-1]`); the doc notes this was checked against SDL 3.4.10 source while the SDK ships 3.5.0, so the
  Windows order is LIKELY but not proven for 3.5.0. A startup warning (`CheckWindowDisplay`, Windows) logs when the
  window did not open on the named display.
- Aspect Ratio row (Display page, after Monitor): `hcu_aspect_ratio`. Graphics rows: `hcu_graphics_adapter` (Windows), `resolution_scale`,
  `hcu_internal_render_scale`, `swap_post_effect`, `hcu_msaa_compatibility`, `anisotropic_override` (see `docs/launcher.md`).
- Guest video mode (`video_mode_*` in `config/hard_corps_uprising.toml`) is fixed and not tied to the window size
  (GAME_ADAPTATION; see [07](07-display-window-frame-pacing.md)).
- VSync caveat (documented): VSync controls tearing only; D3D12 presents with interval 0 and pacing comes from the
  guest `vsync` cvar.

### Launcher window state (commit `e064dd9`, PLATFORM_WINDOWS and PLATFORM_LINUX)

The launcher remembers x, y, width, height, maximized and its own F11 fullscreen as project cvars
(`hcu_launcher_x`, `_y`, `_width`, `_height`, `_maximized`, `_fullscreen`), written with `SaveNow` a moment after the
window settles (`PlacementTracker`, `kPlacementSaveDelaySeconds` 0.5) and when the launcher closes. The pure logic is in
`src/window_placement.h` (`PlacementIsVisible`, `PlanPreShow`, `PlacementTracker`, `FullscreenGate`,
`LauncherFullscreenPreference`) and is covered by `tests/window_placement_tests.cpp` (automated,
`hcu_window_placement_tests`). Behaviors:

- A saved rectangle with too little area on any connected display (monitor unplugged) is ignored; the default placement is
  used, but a saved maximized state still applies. Negative display coordinates are handled.
- Windows: `InstallPreShowPlacement` applies the placement to the next top-level window of the thread before it is first
  shown (no flash). Linux: placed after the window exists; the desktop may ignore it (documented).
- F11 in the launcher toggles the launcher's own borderless fullscreen (`ToggleLauncherFullscreen`); the game window's
  Display mode is independent. A maximized launcher keeps its previous normal size for restore. When Play is pressed
  while the launcher is fullscreen, `LeavingFullscreen` asks to leave it and waits (with a timeout in `FullscreenGate`)
  before building the game window state.
- "Reset all" leaves these keys alone (they are not in `kGameSettingKeys`).

Porting: the separation principle (launcher display state is not game display state) is worth keeping from the start. The
placement tracker is COPY (platform-agnostic) with the small `Win32`/SDL queries in `platform.cpp` adapted.

### In-game cursor auto-hide (`src/window_host.cpp`)

`CursorHider` (5 s, `kCursorHideDelay`) hides the cursor only in focused fullscreen gameplay and shows it again on
mouse movement or whenever a mouse-driven overlay is open; a `CursorTick` ImGui dialog polls it each frame. Tests:
`tests/window_host_tests.cpp` (automated). It lives in `window_host` rather than the launcher but was added with the
launcher state commit.

## 7. Input in the launcher and modal dialogs

- Keyboard and mouse come from ImGui; the launcher turns on `ImGuiConfigFlags_NavEnableKeyboard | NavEnableGamepad` for
  its lifetime and restores the saved flags in `OnClose`.
- Controller: ReXGlue's input system only starts with the game (and an open overlay neuters it), so the launcher
  reads the pads itself through `hcu::ImGuiGamepad` (`src/imgui_gamepad.h`, `src/imgui_gamepad.cpp`; one shared SDL3
  implementation on every platform, the SDL gamepad subsystem started in `Update` and quit in `Release` because SDL
  counts starts and the game's own input is unaffected). All connected pads are read and their inputs combined, so a
  Steam Controller or a DualSense works as well as an Xbox pad. Start = Play; A, B, D-pad, left stick navigate. Stick
  values are mapped with a dead zone to analog key events. Details of game-time input are in
  [08-input-gamepad-mouse.md](08-input-gamepad-mouse.md).
- Quit shortcuts: TRG quits on Escape unless a pop-up is open, so the project ignores a Quit that coincides with an
  Escape press (`hcu::LauncherQuitAllowed`, `src/launcher_quit.h`); Escape only cancels pop-ups and the keyboard
  capture. The footer hint (`config.hints`) therefore advertises Alt+F4, which closes the window through the OS, and
  the Quit button and the title-bar close button still quit. Escape is not a Quit key. The DLC removal confirmation
  (`DrawRemoveDialog`) cancels on Escape in the same frame, because the Escape press can no longer quit; B on a
  controller (`ImGuiKey_GamepadFaceRight`) is Cancel too. The dialog is a real `BeginPopupModal` with the default focus
  on Cancel. CONFIRMED in code and in `tests/launcher_quit_tests.cpp` (Quit filter, footer hint, dialog cancel).
- In-game overlays (cheats menu, achievements, download-content notice) share `ModalDialog` (`src/modal_dialog.cpp`):
  a single open instance, Escape is consumed by a `WindowInputListener` registered above the ImGui drawer
  (z-order 1000 vs 64) so neither ImGui nor the game sees it, controller B closes, and the game receives neutral input
  until every key pressed inside the dialog has been released (`CloseGate`, 1 s timeout so a pad unplugged as the
  dialog closes cannot block input forever). Rules are pure functions in `src/overlay_rules.h` and are tested in
  `tests/live_tests.cpp` (`TestCloseGate`, `TestOverlayRules`, `TestModalOwnership`, automated). Reusable as a pattern
  for any ReXGlue title that overlays ImGui on a running guest.

## 8. Launch process, restart and exit handling

- Play with no window change: in-process. `callbacks.play` applies pending settings and resumes the runtime; the
  launcher dialog closes (`Finish`), saves placement, releases the gamepad.
- Play with a window change: `RelaunchSelf` starts a copy of the same command line plus `--hcu_skip_launcher=true`
  (Windows `CreateProcessW`; Linux `posix_spawn` of `$APPIMAGE` when present, otherwise `/proc/self/exe`, because the
  AppImage mount disappears when the first process exits), then `QuitFromUIThread`. `RelaunchSelf` does not save;
  `RequestPlay` saved before.
- Quit/close: `trg::Result::kQuit` calls `Finish(cb_.quit)` unless a start is in progress or the Quit came from Escape (section 7).
- Achievements page: the launcher builds a throwaway `rex::Runtime` in tool mode (`src/launcher_achievements.cpp`) to read
  the XEX's XDBF and the unlock file, and `StartGame` resets it before Play builds the game runtime. See [11](11-offline-services-profile-achievements.md).
- Game exit: `OnShutdown` detaches input drivers before ReXApp destroys the window, detaches the Escape listener and
  the window host. This is the fix for a use-after-free described in [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md).

## 9. Skip intro, FPS options and other launcher toggles

Skip intro (`50b7948`, `hcu_skip_intro`, `src/skip_intro.cpp`, GAME_ADAPTATION/HARD_CORPS_SPECIFIC): hooks the boot director
(`sub_824E52C8`), and after the original runs, when the setting and a game flag (`0x82C1EEE2`) allow, sets the step
index to the director's completion step so no logo or movie item is entered. The mechanism (hook after init, set the state
machine's index) is generic; every address and offset is title-specific. Tests: `tests/skip_intro_tests.cpp`
(`hcu_skip_intro_tests`: setting, launcher row and step choice without game memory). The memory notes record the
intro-to-title transition itself as not fully proven; in-repo documentation (`docs/launcher.md`) states it reaches the title
through the game's own transition (manual).

Frame rate in title (`hcu_show_fps`), in-game frame counter (`hcu_show_frame_counter`, F2 toggles for the session;
`274ee61`, tests `frame_counter_tests.cpp`): app-side window features, see [07](07-display-window-frame-pacing.md).

## 10. About page, app icon, achievements viewer

- About: description, "Open game folder" and "Open data folder" (`trg::OpenInFileManager`, uses `ShellExecute` on
  Windows and `xdg-open` on Linux), "Show settings file" (saves first, opens the file or the folder when the file does not
  exist yet), Reset all, port version (`HCU_PORT_VERSION`), library versions, title ID and game version, repository
  URL. The "Saves and settings" help text comes from `UserDataFolderHelp(outcome)` so a failed migration is visible to the
  player (see [10](10-user-data-settings-migration.md)).
- App icon (`26bd37c`): Windows resource (`packaging/windows/hard_corps_uprising.rc`, `packaging/icons/app-icon.ico`),
  generated from one SVG by `tools/make-app-icon.py`. Linux: `SetApplicationId` sets the SDL app ID before window creation
  (Wayland app_id and X11 class `io.github.saulob.hard-corps-uprising`), `ApplyWindowIcon` loads the PNG from the
  AppImage's hicolor tree because SDL does not take it from a resource. Packaging side in
  [14-packaging-release.md](14-packaging-release.md). Confidence CONFIRMED; no automated test beyond the packaging
  check in `tools/package-linux.sh` that app ID and `.desktop` agree.
- Achievements page: read-only list with summary (unlocked count, gamerscore progress bar) and a TRG achievement grid,
  icons as textures on the immediate drawer. Disabled with a message when game files are not ready or the list cannot
  be read. Test: `TestLauncherAchievementList`, `TestLauncherPageWiring` in `tests/live_tests.cpp`.

## 11. Errors the player sees

- Install errors: `DescribeInstall` maps each `InstallStatus` to a title and a plain sentence; the Play page keeps the
  failure visible (`install_failure_`) and a transient status line. Technical details go to the log only.
- Different version of the game: amber "attention" card, text from `kWrongVersion`.
- Settings could not be saved: Play refused, launcher stays.
- Relaunch failed: launcher stays, message.
- Graphics failure (`6cf59bd`, PLATFORM_LINUX): `SetupPresentation` is overridden; when the base call fails the app
  calls `hcu::ReportGraphicsFailure()`, which on Linux shows an error window ("graphics system could not start ... needs a
  working Vulkan graphics driver ... log folder next to the AppImage") drawn in an SDL surface. It needs no GPU, which is
  the point: the SDK only logs the failure, so before the commit a machine without Vulkan exited silently. SDL's message box
  is not used, because it runs zenity first when zenity is installed. Windows has an empty implementation (D3D12 failure
  is not reported by this function). Manual only, no test; classification REUSABLE_INFRASTRUCTURE, CONFIRMED from the commit,
  UNCERTAIN on how every Linux session manager renders the box.

## 12. Evolution by commit

| Commit | Date | Change |
| --- | --- | --- |
| `8d6558b` | 2026-10-10 | F11 fullscreen toggle and title-bar FPS in the game window (`window_host`), before any launcher |
| `dc89a6c` | 2026-10-10 | TRG launcher, `GameSettings`, `platform.cpp`, legacy `on`/`off` repair, shipped TOML changes |
| `5ee45cf` | 2026-10-10 | Launcher integration review: launcher window size comment/limits revised, "Show settings file" falls back to the data folder when the file does not exist |
| `b31b959` | 2026-10-10 | Package installer (`stfs`, `game_files`), install page, 705-line installer tests, Windows package script |
| `9964f82` | 2026-10-11 | Linux build: platform split in `platform.cpp`, SDL gamepad, `RelaunchSelf` via `posix_spawn`, STFS non-regular-file fix, AppImage |
| `f66eeca` | 2026-10-11 | DLC install, uninstall and DLC page, plus tests |
| `d1f1a19` | 2026-10-11 | DLC review: safer work folders, reparse-point rules |
| `f3ac390` | 2026-10-11 | Offline Live, achievements page, shared `ModalDialog` |
| `28c0251` and earlier cheat commits | 2026-10-11 | Cheats page and F10 menu; session overrides separate from saved cvars |
| `26bd37c` | 2026-10-12 | App icon and Linux app ID |
| `27e26ea` | 2026-10-12 | Portable `user_data`; launcher shows the outcome |
| `274ee61` | 2026-10-12 | Frame counter option; achievements overlay close button |
| `e064dd9` | 2026-10-12 | Launcher window state, launcher F11, cursor auto-hide |
| `50b7948` | 2026-10-13 | Skip intro |
| `cf6eee1` | 2026-10-14 | Multisampling option reworked to `hcu_msaa_compatibility`; performance and pacing batch |
| `6cf59bd` | 2026-10-15 | Error window when Vulkan is unavailable |

Superseded approach worth knowing: first versions of the settings UI wrote `key = on` / `key = off`, which TOML rejects, and
ReXGlue then ignored the whole file; `RepairLegacyToggles` (`src/legacy_settings.cpp`) fixes exact bare values only, keeps
`settings.toml.bak`, and writes through a temp file and rename (`dc89a6c`). The comment in the code and `docs/launcher.md`
say the bad values came from an unreleased development build. Moral: validate the file you write against the parser that
reads it (see section 5 of [10](10-user-data-settings-migration.md)).

## 13. What to port first, what is optional

First (smallest set that makes a game installable and playable):

1. Lifecycle skeleton with `OnFinalizePaths` returning `nullopt` and `resume`, and the `LauncherWanted` rule that forces the
   launcher when game data is missing.
2. `CheckGameFiles` equivalent (size plus CRC of the one supported executable).
3. Install with staging next to the target, verification, and swap with backup; cancel support; cleanup guard.
4. `GameSettings`-style pending/saved model, project cvar registration, write only changed keys to a player file,
   refuse unregistered keys, two-phase capture.
5. Window separation: decide early whether the launcher shares the game's window; if it does, implement the relaunch rule
   and `--skip_launcher` one-shot flag.
6. Error messages for install failures and graphics-init failure.

Optional: placement memory and launcher F11, controller navigation, DLC page, achievements page, About helpers, icon,
cursor auto-hide, skip-intro and cheat pages.

Never copy blindly: the 853x480 vs 1280x720 defaults, window size list, DLC content IDs, title ID, CRC, skip-intro addresses.

## 14. Pitfalls checklist

- Escape must not quit the launcher: filter the TRG Quit result by the Escape key (section 7).
- Reading cvars before the GPU plugin registers them gives empty values (section 5).
- Launcher on `monitor` N: use `monitor = 0` for the launcher window and the saved value for the game.
- Linux needs `zenity` or `kdialog`; there is no fallback (section 4).
- A relaunch must pass a one-shot flag that is reset on read so it is never saved.
- Antivirus can delay renames of freshly extracted folders (retry loop, section 3.2).
- Never delete a work folder without a reparse-point check.
- `hcu_installer_tests` and the other test targets are `EXCLUDE_FROM_ALL`; build them explicitly.

## 15. How to verify

Automated (build target names from `CMakeLists.txt`): `hcu_installer_tests` (STFS, install, DLC), `hcu_window_placement_tests`,
`hcu_window_host_tests`, `hcu_skip_intro_tests`, `hcu_multisampling_tests`, `hcu_launcher_quit_tests` (Escape does not quit),
`hcu_live_tests` (overlay rules, launcher
achievement list). Manual: install from a real package on Windows and Linux, press Play with default settings (observe one
relaunch), unplug the secondary monitor with a saved placement, press Escape in the DLC removal dialog (it cancels and the
launcher stays open), and confirm Alt+F4, the title-bar close button and the Quit button close the launcher.

Confidence summary: launcher architecture, install pipeline, DLC pipeline and settings model CONFIRMED (code plus tests);
Linux launcher behavior LIKELY (experimental per `docs/linux.md`; a SteamOS tester ran it); the Vulkan-unavailable window
CONFIRMED in code, UNCERTAIN in presentation; monitor ordering on Windows with SDL 3.5.0 UNCERTAIN.
