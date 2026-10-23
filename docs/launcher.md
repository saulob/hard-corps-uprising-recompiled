# Launcher

The pre-game launcher is [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), integrated the same way as Earthworm Jim HD.

## Architecture

- Embedded, not standalone. TRG draws inside ReXGlue's own Dear ImGui frame, in the game window. There is no second window, process, SDL or ImGui.
- Dear ImGui is the copy inside `rexruntime` (1.92.5). CMake passes it to TRG as `TRG_LAUNCHER_IMGUI_TARGET`, with `TRG_LAUNCHER_STANDALONE`, `TRG_LAUNCHER_FETCH_DEPS` and `TRG_LAUNCHER_BUILD_DEMO` off, so nothing is downloaded.
- Lifecycle, from `ReXApp`:
  1. `SetupEnvironment` loads `hard_corps_uprising.toml`; `OnPostInitLogging` loads the player's `settings.toml` over it. The Xenos GPU plugin is not loaded here. ReXGlue loads it in `SetupPresentation`, and the Graphics cvars it registers take the player's values from the file.
  2. `OnPreSetup` records the saved game settings and decides whether the launcher shows. If it does, `PrepareLauncherWindow` sets the window for the launcher. Then the window settings this process creates its window with are recorded. Graphics keys are not registered yet, so they are recorded as unregistered.
  3. `SetupPresentation` creates the window and loads the GPU plugin. `OnFinalizePaths` calls `CaptureLateRegistered`, which takes the Graphics values now that they are registered, then shows the launcher and returns `nullopt`; the runtime is not built yet. A key the player has already edited in this process is never overwritten.
  4. **Play** (mouse, Enter or Xbox Start) calls `RequestPlay`, which is the only save: if it fails, the launcher stays open with "Could not save the settings." and nothing starts. On success it shows "Settings saved. Starting game..." at full brightness for 0.4 s, timed with ImGui and without sleeping. The launcher's controls are dimmed and disabled during that time, and its keyboard, gamepad and mouse input is released each frame, so Play, Save, Ctrl+S, Esc and the D-pad do nothing. Then, if the window size, monitor, VSync or graphics card differs from this process's window, the game is relaunched once and this process quits (`StartGame` does not save). Otherwise the pending settings are applied and the runtime starts.
  5. **Relaunched copy**: `--hcu_skip_launcher=true` skips the launcher, and the window is created natively with the saved settings.

## Settings

| File | Written by | Role |
| --- | --- | --- |
| `<exe dir>/hard_corps_uprising.toml` | the build (copied from `config/`) | shipped defaults, never written by the launcher |
| `<user data>/settings.toml` | the launcher | the player's choices, loaded last |

- Normally user data is the `user_data` folder next to the program ([user-data.md](user-data.md)). `tools\build.ps1 -Run` passes `--user_data_root`, which wins, so user data is `build\user_data` (kept by `-Clean`) and nothing migrates.
- Launcher edits are pending in `hcu::GameSettings` until Save or Play. Save writes only the changed keys to `settings.toml`, through `trg::TextFileSettings`; comments and other lines are kept. A key whose cvar is not registered is refused rather than written.
- Play is blocked, and does not save, while the game files are missing or invalid or an install is running.
- Reset all settings puts the launcher's values back to the shipped ones, pending until Save or Play.
- Legacy repair: an unreleased development build wrote `key = on` and `key = off`, which TOML rejects and which made ReXGlue ignore the whole file. Before the file is parsed, each exact bare `on`/`off` value is rewritten as `true`/`false`. Nothing else changes. The original is kept as `settings.toml.bak`. Once the file holds no bare on/off, this does nothing.
- The dependency is the `third_party/trg-launcher` submodule, pinned to `b384a28` (TRG Launcher 1.1.0). After cloning, run `git submodule update --init third_party/trg-launcher`. The submodule is never modified: CMake copies the clean checkout to `<build>/trg-launcher-patched` at configure time, applies the patches in `patches/trg-launcher` (the hyperlink integration) there, and compiles that copy. It fails when the submodule is at another revision or carries local changes. When the submodule is not checked out, CMake falls back to the checkout named by `HCU_TRG_LAUNCHER_DIR` (CMake cache or environment), copies it the same way and applies the patches without modifying it. The upstream repository has no LICENSE file.

## Window

The launcher window and the game window are separate.

- **Launcher window:** Windowed, 1280×720 logical (capped at 90% of the primary display's work area), centred on the primary display by SDL (`monitor = 0`).
- **Game window:** created by the game process with the saved Window size, Monitor, VSync and Display mode.

| Setting | When it takes effect |
| --- | --- |
| Window size, Monitor, VSync, Graphics card | Play. If any differs from the launcher's window, the game restarts once in a new window. |
| Display mode | Play. In the launcher's window when the window already matches, otherwise in the new window. |
| Aspect Ratio, graphics, frame counter, keyboard | Play, in the same window |

### Window position

The launcher reopens at the position and size it was closed with, and maximized if it was maximized. A maximized launcher keeps its previous normal size for when it is restored. The values are saved a moment after the window settles and again when the launcher closes, as `hcu_launcher_x`, `hcu_launcher_y`, `hcu_launcher_width`, `hcu_launcher_height` and `hcu_launcher_maximized` in `settings.toml` ([user-data.md](user-data.md)). Reset to defaults leaves them alone.

If less than a small corner of the saved window lies on a connected display (for example a monitor was unplugged), the launcher opens in its default place instead; a saved maximized state is still applied. Displays left of or above the primary one are handled. On Windows the window is placed before it is first shown; on Linux right after it opens, and the desktop may ignore the position. **F11** in the launcher switches the launcher itself to borderless fullscreen and back, to the same window or maximized state. If the launcher is closed while in that fullscreen it reopens in it (`hcu_launcher_fullscreen`), and F11 returns to the saved window or maximized state. Playing leaves it for the game without changing that choice; only F11 does. The game window is not affected: it opens as the Window size and Monitor settings say, and Display mode is not part of the saved position.

Why a restart: ReXGlue 0.10.0 has no API to resize or move an open window (its `window.h` lists resize as not implemented). The window's size and display are fixed when it is created. A window created with the game's size and display keeps the right Windowed size and position for F11.

- Default game window: 853×480 logical, from `config/hard_corps_uprising.toml`. A value in `settings.toml` replaces it.
- With the defaults, a normal Play from the launcher relaunches the game once. The launcher and the game have independent window sizes, so this is expected, not an error. The relaunched copy opens its window at the game size directly.
- Display mode is switched in place only when the window already matches the game's size, monitor and VSync. Otherwise it is applied by the relaunched copy, so the Windowed size F11 restores is always the game's.
- The launcher's 1280×720 target is capped to the usable desktop, so on a display where it does not fit (for example 2560×1440 at 225%, where it becomes 1024×576) the launcher is smaller.
- Sizes that cannot fit the current display at its scaling are hidden from the Window size list, except the current one.
- **Aspect Ratio** (Display page, after Monitor) is the game's target rendering format, in one dropdown: 16:9 Original (default), 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4. It applies when Play starts the game. The Window size list shows the sizes of the chosen format; changing the format moves Window size to that format's default unless the current size already belongs to it. The value is saved as `hcu_aspect_ratio` in `settings.toml`; a file without it means 16:9 Original.
- A `present_letterbox` line from an older `settings.toml` is removed when the game starts. The presenter always letterboxes the chosen format, so there is no Stretch mode.

### Aspect Ratio and the guest picture

The game renders 720 lines at the chosen format's width: 1280 at 16:9, 1152 at 16:10, 1680 at 21:9, 2560 at 32:9, 960 at 4:3 and 900 at 5:4. The guest video mode is 480 lines at the same ratio: 853×480 at 16:9, 1120×480 at 21:9. Window size changes only the host window. The game computes its own canvas for the format, so the picture is not a stretched 16:9 frame, and a wider format shows more of the game world at the sides as far as the game draws it. Menus and videos the game composes for 16:9 are not reworked; the presenter letterboxes the whole picture to the chosen format.

The presented output is the render base scaled by Render resolution. At 16:9 it is measured as 1280×720, 3840×2160 at scale 3 and 5120×2880 at scale 4.

### Relaunch rule and loop prevention

- The comparison is between the pending settings and the window this process created. The launcher's own window is what this process created, so it counts as a mismatch when the game's settings differ from it.
- A relaunch starts a copy with `--hcu_skip_launcher=true`. That copy never shows the launcher, so it never compares or relaunches.
- Launcher hidden: the game starts directly with the saved settings.
- Shift at start shows the launcher with its own window. The relaunched copy skips it even if Shift is still held.

### Monitor numbering

- ReXGlue's `monitor = N` opens the window on `SDL_GetDisplays()[N-1]`. SDL lists the primary display first, then the others in Windows order.
- The launcher's Monitor list uses the same order (`ListMonitors` in `src/platform.cpp`). The order was checked against the SDL 3.4.10 source; the SDK ships SDL 3.5.0, whose source is not on disk.
- At startup the game logs a warning when the window did not open on the display the setting names.
- Monitor `0` (Default) is the primary display.

## Game files and install

- **Game folder:** `--game_data_root`, else `game` next to the program (`HardCorpsUprisingApp::OnConfigurePaths`). Development builds pass `game\extracted`. The folder holds `default.xex`, `DataXENON\`, `ArcadeInfo.xml` and `nxeart`, and nothing the player makes. Settings, saves and the shader cache are in the user data folder (`user_data` next to the program, or `--user_data_root`); logs are in `logs` next to the program.
- **One readiness check:** `CheckGameFiles` (`src/game_files.cpp`) is used by the Play page, the install verification and the clean-start decision. Ready means `default.xex` is the supported build (12,955,648 bytes, CRC32 `3DF903B9`) and `DataXENON` exists. Missing and unsupported are separate states.
- **Launcher visibility:** `LauncherWanted` (`src/launcher.h`) shows the launcher whenever the game files are not ready, whatever `hcu_launcher` or `--hcu_skip_launcher` say, so turning the launcher off cannot hide the only way to install the game. A relaunch after PLAY still skips it, because the files are ready then.
- **Install from package** (`InstallFromPackage`, on a TRG `Task`, so the UI stays responsive): reads the STFS header and requires Title ID `58410A18`, reads and validates the file table, checks free space, extracts into `game-install-temp` next to the game folder, runs `CheckGameFiles` on it, and only then swaps it in (`game` becomes `game-install-backup`, the staging folder becomes `game`, the backup is deleted). A wrong package, damaged package, bad `default.xex`, write error or Cancel leaves the existing game untouched and removes the staging folder. A folder that holds other files is never replaced.
- **DLC install** (`InstallDlcFromPackage`, on the same kind of `Task`): requires Title ID `58410A18` and content type `00000002`, rejects the game package (content type `000D0000`) and other titles, and names the folder by the package's content ID from its metadata, not by the file name. It extracts into `dlc-install-temp` under the user data folder, writes the `.header` the SDK's content manager reads, and swaps both in with the previous install kept until the swap completes. A failed, cancelled or clashing install leaves the installed DLC as it was. It also stops, changing nothing, when `dlc-install-temp` or the installed copy it would replace is not a plain folder (a junction, symbolic link or other reparse point at or inside it), and it only ever deletes `dlc-install-temp` after the same check. The layout is the SDK's marketplace content layout for this title: `<user data>\0000000000000000\58410A18\00000002\<content ID>\` with `Headers\00000002\<content ID>.header` beside it. The game folder is never touched.
- **DLC page** (sidebar, directly after Play): one card per supported character, Harley Daniels, Sayuri and Leviathan, and an "Other installed content" section for verified packages no character card shows. Each card shows Installed (green, with a Remove button) or Not installed (neutral). The state is read from the installed packages on every change, never stored. The character comes from the package's content ID when it is one of the three known IDs, else from its English display name: exactly one of the three names, optionally followed by "Additional Character" with or without parentheses; any other package is shown under Other installed content. Install DLC... runs the install above and is available whether or not the game is installed.
- **DLC removal** (`RemoveInstalledDlc`, the Remove button on a card, after a confirmation dialog with Cancel and Remove): removes only the selected package's content folder and its header, identified by content ID. Before anything is moved it requires that the package is verified (its header names this title, content type `00000002` and the same content ID), that its folder is a plain folder directly inside the DLC storage (no junction, symbolic link or other reparse point, here or inside it), and that the game folder is neither the folder nor inside it. The folder is moved aside first, then the header is deleted; if the header cannot be deleted the folder is moved back, and the result is reported as a failure. The state is read back from the installed packages before success is reported. Saves, settings, the shader cache, the game folder and other Marketplace content are never touched. Nothing is removed for a malformed content ID, a package that does not verify, or a package that is not installed.
- **Package reader** (`src/stfs.cpp`): read-only STFS (`LIVE`, `PIRS`, `CON `) with consecutive files, which is what Xbox Live Arcade packages are. Every entry name is checked before anything is written: one printable ASCII path component, no `..`, separators, drive or stream markers, device names, or trailing dot or space; parents must be directories and nesting is limited. Extraction also checks that each destination stays inside the staging folder. Block hashes and the Microsoft signature are not verified.
- **Tests:** `cmake --build build --target hcu_installer_tests` builds `tests/installer_tests.cpp`. It builds synthetic packages (with a `default.xex` of the supported size and CRC32 made from generated bytes), so it needs no game files. `hcu_installer_tests --package <file> --reference <extracted folder>` also extracts a real package once into a temporary folder and compares it.

## Pages

| Page | Setting | Cvar |
| --- | --- | --- |
| Play | Install / Reinstall from package | (game files, see below) |
| | Show this launcher | `hcu_launcher` |
| DLC | Install DLC..., Remove | (DLC packages, see below) |
| Display | Display mode | `fullscreen` |
| | Window size | `window_width`, `window_height` |
| | Monitor | `monitor` |
| | VSync | `d3d12_allow_variable_refresh_rate_and_tearing` (inverted) |
| | Aspect Ratio | `hcu_aspect_ratio` |
| Graphics | Graphics card (Windows) | `hcu_graphics_adapter` |
| | Render resolution | `resolution_scale` |
| | Internal render resolution | `hcu_internal_render_scale` |
| | Anti-aliasing | `swap_post_effect` |
| | Multisampling | `hcu_msaa_compatibility` |
| | Texture filtering | `anisotropic_override` |
| Gameplay | Skip intro | `hcu_skip_intro` |
| | Frame rate | `hcu_frame_rate` |
| | Frame rate in title | `hcu_show_fps` |
| | Render resolution in title | `hcu_show_render_resolution` |
| | Frame counter | `hcu_show_frame_counter` |
| | Player 1, Player 2 | `hcu_player_1_name`, `hcu_player_2_name` |
| Cheats | Infinite Health | `hcu_cheat_health` |
| | Infinite Lives | `hcu_cheat_lives` |
| | Rapid Fire | `hcu_cheat_rapid_fire` |
| | Jump Height 2x | `hcu_cheat_jump_height` |
| | Keep Weapons on Hit | `hcu_cheat_keep_weapons` |
| | Movement Speed 2x | `hcu_cheat_movement_speed` |
| | Super Armor | `hcu_cheat_super_armor` |
| Controls | Keyboard | `mnk_mode` |
| | Keyboard controls | `keybind_*` (one per action), reset by Keyboard defaults |
| About | Detailed logging | `detailed_logging` |

### Graphics

| Control | Cvar | Values (default) |
| --- | --- | --- |
| Graphics card (Windows) | `hcu_graphics_adapter`, sets `d3d12_adapter` | the DXGI adapter index; the first detected adapter when unset or not present (`0`) |
| Render resolution | `resolution_scale` (integer 1–8, the launcher offers 1–4) | `1` Original, `2`, `3`, `4` (`1`) |
| Internal render resolution | `hcu_internal_render_scale` (percent of the render resolution) | `100` 100% Native, `75` 75% Performance, `50` 50% Low-end (`100`) |
| Anti-aliasing | `swap_post_effect` | `none` Off, `fxaa`, `fxaa_extreme` (`none`) |
| Multisampling | `hcu_msaa_compatibility` | `false` 2x MSAA, `true` 4x MSAA (Compatibility) (`false`) |
| Texture filtering | `anisotropic_override` | `-1` Game (no override), `0` Off, `2` 2x, `3` 4x, `4` 8x, `5` 16x (`3`, 4x) |

Multisampling only decides how the render targets the game itself draws with 2x MSAA are drawn on the host: native 2x MSAA, or 4x MSAA using two of its samples, for GPUs or drivers whose 2x MSAA misrenders. It cannot turn the game's multisampling off, and the compatibility choice costs more GPU time. The game sets the Xenos `native_2x_msaa` from it at every start, also replacing a `native_2x_msaa = false` that older launchers wrote for "Off".

Shader compilation is not a launcher setting; the game uses ReXGlue's default, `async_shader_compilation = true`.

- **Graphics card** lists the hardware GPUs Windows reports that can create a Direct3D 12 device, by the names DXGI gives them; software adapters are not listed. It is shown on Windows only.
  - Its DXGI index, counted as ReXGlue counts for `d3d12_adapter` (`EnumAdapters1` order), is passed as `d3d12_adapter`. A missing key, the retired `auto` or a GPU that is no longer present selects the first listed GPU.
  - The graphics device is created before the launcher shows, so a change restarts the game once on Play, like a window change.
  - The log shows the choice (`Graphics card: <name> (adapter N)`) and, from ReXGlue, the GPU actually used (`DXGI adapter: <name>`).
  - Linux has no such row: `vulkan_device` indexes the instance ReXGlue creates, which the launcher cannot reproduce reliably, and Vulkan keeps its automatic discrete-GPU preference.
- **Render resolution** labels show the presented output at each scale for the chosen Aspect Ratio: at 16:9, Original 1280×720, 2x 2560×1440, 3x 3840×2160, 4x 5120×2880. They do not depend on Window size. 2x is a natural match for 1440p displays. The recommendation is informational only, and the default stays Original. Higher scales cost more GPU time.
- The game takes the render resolution when its GPU starts, after Play, so a change applies to the game that Play starts. The GPU driver can lower a scale it cannot support; the game logs a warning when it does.
- Render resolution is not a relaunch trigger. It never changes the window.

Internal render resolution is the resolution the GPU draws the game at before the picture is scaled to the selected Render resolution. Render resolution keeps selecting the target; this setting only reduces the work behind it, and says nothing about the window.

- **100% Native** is the current behavior, unchanged: the game is drawn at the Render resolution. A missing or unknown value is 100%.
- **75% Performance** is the recommended choice for a GPU that is too slow at the Render resolution. **50% Low-end** is meant for low-end hardware. Lower values are less sharp. No frame rate gain is claimed; measure it on the GPU in question.
- The internal size is the Render resolution times the percentage, in the chosen Aspect Ratio's render base, so the ratio is kept: at 16:9, 1280×720 at 75% is 960×540, at 50% 640×360; 2x is 1920×1080 and 1280×720; 3x is 2880×1620 and 1920×1080; 4x is 3840×2160 and 2560×1440; at 21:9 the 1680×720 base gives 1260×540 at 75% and 840×360 at 50%. The game logs one line with it at start, for example `Internal render resolution: 75% 960x540 -> 1280x720`.
- On Windows a value below 100% also makes the D3D12 render target path the host one (`render_target_path_d3d12 = rtv`) when that setting is empty, which replaces the rasterizer-ordered path Intel GPUs use by default; the game logs `Internal render resolution: using D3D12 host render targets`. 100% leaves the path selection alone, and a `render_target_path_d3d12` set by hand is never overridden.
- Like Render resolution, the game takes it when its GPU starts, after Play; it is not a relaunch trigger and never changes the window, the guest video mode, the aspect ratio or VSync.
- It is a separate setting from `resolution_scale`; Texture filtering, Anti-aliasing and Multisampling are independent of it. The runtime patches that implement it (and the cases where it draws at the full Render resolution instead) are in [rexglue-runtime-patch.md](rexglue-runtime-patch.md#why-0036-0037-and-0038).

- Every default is the value the game already used.
- VSync controls tearing only. The D3D12 presenter always presents with sync interval 0; frame pacing comes from the guest `vsync` cvar, which the launcher does not touch.
- Keyboard gameplay input can be turned on or off, and the bindings edited, on the Controls page (see below).
- Any controller SDL recognizes drives the launcher (a Steam Controller, a DualSense or an Xbox pad): Start plays; A, B, the D-pad and the left stick navigate. ReXGlue's input system only starts with the game.
- The accent is a neon green (`kAccent`) with a dark emerald for the ready card, both in `src/launcher.cpp` with the header gradient. Amber is kept only for the "game files" warning, which is a status colour.

### Gameplay

| Control | Cvar | Values (default) |
| --- | --- | --- |
| Skip intro | `hcu_skip_intro` | `false` Off, `true` On (`false`) |
| Frame rate | `hcu_frame_rate` | `30`, `60` Original, `90`, `120`, `144`, `165`, `240`, `0` Unlimited (`60`) |
| Frame rate in title | `hcu_show_fps` | `false` Off, `true` On (`true`) |
| Render resolution in title | `hcu_show_render_resolution` | `false` Off, `true` On (`false`) |
| Frame counter | `hcu_show_frame_counter` | `false` Hidden, `true` Shown (`false`) |
| Player 1, Player 2 | `hcu_player_1_name`, `hcu_player_2_name` | up to 15 characters and 15 UTF-8 bytes (`Player 1`, `Player 2`) |

- **Player names** change only the name the game shows for local players 1 and 2; an invalid saved name falls back to the default. The XUID and the profile and save directory never change. A name left at its default keeps the game's own name for that user.
- **Skip intro** starts directly at the title screen: the game's boot sequence starts at its last step, so the startup logos, the opening movie and the notices are not shown, and the game then reaches the title screen through its own transition. It applies only to the startup presentation of each start; nothing is written to the game files or the save data. The hook is in `src/skip_intro.cpp`; `hcu_skip_intro_tests` checks the setting, its launcher row and the step choice without game memory.
- **Frame rate** is a Choice of eight values in two rows. 60 Original is the game's own pacing. 30 runs the game at normal speed at 30 frames per second. 90 to 240 and Unlimited present that many frames per second during stage gameplay while the simulation stays at 60 Hz, with automatic visual interpolation of the objects and the camera between steps. A stored value that is not one of the eight choices reads as 60. Menus, loading and pause stay at 60. See [display.md](display.md#frame-rate).

### Cheats

The sidebar order is Play, DLC, Display, Graphics, Gameplay, Cheats, Achievements, Controls, About. Achievements is read-only: it lists the game's 12 achievements and which are unlocked (see [offline-live.md](offline-live.md)). All seven cheats default to off and are ordinary launcher settings: saved with Save or Play, and put back to off by Reset all settings. The launcher value is the start of every game session; the F10 menu (below) changes the cheats for the running session only.

- The hooks are in `src/cheats.cpp` and ask `hcu::cheats::Enabled` when they run, so turning a cheat off restores the game's own behaviour with no other state to undo. Nothing is written to the game files or the save data.
- A hook acts only on an object whose vtable is the game's player class (`hcu::cheats::kPlayerVtable`), which is every character, DLC included, in both player slots. Enemies use other classes.
- **Infinite Health** turns a negative life delta into zero in the player's two add-life methods, which is how hits arrive. Healing and the death commands that set life to zero directly (`SetLife(0)`) are untouched.
- **Infinite Lives** skips the player's stock decrement on respawn and refuses a set-stock call that would lower the stock. The stock can still go up.
- **Rapid Fire** halves the shot cooldown, in frames, that the game's own script sets after each shot, so weapons keep their relative speeds. The script command and the player's cooldown setter are hooked together, because the per-frame countdown uses the same setter. Odd values alternate between rounding down and up, so the average is exactly half. Sayuri's attacks use the same cooldown.
- **Jump Height 2x** scales the launch speed of the game's jump (`Obj::Jump`, shared by every object class, so the hook acts on players only) by the square root of two. Gravity is constant (0.1419 per frame), so the apex grows with the square of the launch speed. Each character's own speed is scaled, including Sayuri's and Leviathan's special jumps. Horizontal speed is not touched, and moves that set a velocity directly instead of calling the jump are not scaled.
- **Keep Weapons on Hit** disarms the weapon drop that a hit arms. The player's on-damaged script then neither resets the weapon nor spawns the dropped item. A hit that leaves the player at zero life still drops the weapon, as in the game. It does not depend on Infinite Health.
- **Movement Speed 2x** doubles the horizontal speed that a character's own script sets for ordinary movement: walking on the ground (0.656 for all five characters, also along slopes and when walking backwards with the aim locked), steering in the air (0.656 Bahamut and Leviathan, 0.816 Krystal, 0.608 Harley, 0.704 Sayuri) and Run (1.6, and 1.76 in some Krystal and Sayuri run states). The two script commands that set a velocity (4504 horizontal, 4503 vector) are hooked together with the object's velocity setters, so only a call made by such a command on the player's own script is doubled, and only when its speed is one of those values. Run is recognised only in the run states of the shared script layout (99, 101 to 103, 164 to 168, 170 to 173), because Sayuri's slide (state 110) also sets 1.6. A slope vector doubles both parts so the path stays the same; a run vector doubles only the horizontal part, so a run jump's launch is unchanged. Dashes in other states, slides, the Sayuri and Leviathan special moves, knockback and anything that moves enemies, platforms or projectiles use other values or other paths and are not touched. The animation, gravity and the game's speed stay as they are.
- **Super Armor** lets a living player take a hit's damage without the push or the hit state. The hit handler (`sub_824B2728`, source and victim) applies the damage and weapon drop, then picks a hit state (the ids come from the player script header: 73, 74, 79 and 80 for Bahamut) and sets the push. For an armored player two hooks steer it down its own paths: the accumulated-damage getter (`sub_8240DD00`) reports a value below the stagger threshold, so the handler skips the state change exactly as it does for a hit an enemy's armor absorbs, and the push predicate (`sub_824B4398`) says no, which skips the velocity, decay and gravity writes. Collision, hit detection, damage, weapon loss and the other cheats are untouched. A hit that kills the player is the game's own, with its reaction and push. A player that took an armored hit also ignores further hits for one second (`kHitGraceSeconds`), because the hit state is what normally stops the next pellet of a volley or sustained contact. Hits that carry the forced-state flag (`0x20` of the hit data's second flag word), grabs, scripted movement, dashes and jumps use other paths and are not changed. All five characters share the player class and the handler.
- `hcu_cheats_tests` (`cmake --build build --target hcu_cheats_tests`) checks the player test, the life delta, the stock rule, the shot cooldown, the jump speed, the weapon-drop rule, the movement speeds, the Super Armor rule and the re-hit window and the session overrides without game memory.

#### In-game cheat menu

Press **F10** while playing to open the cheat menu in the top-right corner, and F10 or Escape to close it. Up and Down (or the D-pad) move the selection, Enter (or A) toggles the selected cheat, B closes the menu, and the mouse works too. The menu lists the same seven cheats as the Cheats page. Auto-repeat is ignored. F10 is used by nothing else in the port, ReXGlue or the game's keyboard layout.

- It is session-only. The values the launcher saved are the start of every session (`hcu_cheat_*` cvars, written to `settings.toml` only by the launcher). A change made in the menu is kept as a per-cheat override in memory (`hcu::cheats::SessionCheats`), applies at once, and is gone when the game closes. Turning a cheat back to the launcher's value drops the override. Nothing in the menu calls `GameSettings::Save` or `rex::cvar::SaveConfig`.
- Why not set the cvars: `rex::cvar::SaveConfig` writes every modified cvar, and ReXGlue's own settings overlay (F4, "Save to config") calls it, so a cvar changed by the menu would be saved to `hard_corps_uprising.toml` the next time someone pressed that button. The hooks read the effective value, so the cvar stays the launcher's value.
- The menu is a `rex::ui::ImGuiDialog` on the drawer ReXApp already owns (the one the launcher and F3/F4 overlays use), in `src/cheat_menu.cpp`. It does not add a second ImGui, backend or window. Its F10 and Escape handling is a window input listener above the drawer, so those key presses are consumed there.
- While it is open the game receives no keyboard or controller input: the input system's "active" callback (the one ReXApp installs for its overlays, repeated with the menu's condition) reports inactive, and ReXGlue's drivers then report an untouched controller. The keys and buttons pressed while the menu was open keep the game blocked after it closes, until they are released (at most one second), so the A, Enter or B used in the menu is never seen by the game as a held button.
- The controller is read through `hcu::ImGuiGamepad`, the same helper the launcher uses (SDL gamepads on every platform, so any pad SDL recognizes — a Steam Controller included — works).

### Controls

The Controls page opens with **Keyboard**, which turns ReXGlue's keyboard-to-pad emulation on or off; it is on by default. The controller works either way, and the port's own hotkeys (F2 frame counter, F3 and F4 overlays, F7 achievements, F10 cheat menu, F11 fullscreen, the console) do not depend on it.

Below it, one row per Xbox pad action: A, B, X, Y, the bumpers, the triggers, the sticks and their presses, the D-pad, and View / Menu. Guide is left out, as the pad ignores it. Click a key, then press the new one; Esc cancels. The launcher captures one key per action: a held Shift, Ctrl or Alt is not recorded as a modifier, so a binding such as the default `Shift+Up` can be restored only with Keyboard defaults. A stored binding with modifiers is shown and works in the game.

- While Keyboard is Off the rows are greyed out, but the mappings are kept and come back when it is turned on again.
- The bindings are ordinary launcher settings: saved with Save or Play, and put back by Reset all settings. **Keyboard defaults** restores exactly ReXGlue's pinned defaults, including the alternatives some of them use (`Semicolon,Space` for A, `X,Return` for Menu), and does not change the Keyboard On/Off choice. Capturing a key replaces the whole binding with that one key, so the alternatives are dropped on a rebind.
- They are stored as ReXGlue `keybind_*` cvars, so the same settings file also sets them when the launcher is skipped.
- Keys the port already uses are refused with a message: Escape, F2, F3, F4, F7, F10, F11, F12 and the backtick console key.

### About

The About page ends with a **Diagnostics** section. The launcher and the game share one `hard_corps_uprising_NNN.log` for the whole application session, including the relaunch that creates the game window. When the game is about to run, the log receives a compact `Runtime configuration` block: the Launcher Display, Gameplay and Graphics choices as the player made them, followed by the effective runtime OS, CPU, GPU, display, game identity, timing, renderer and input state. Selected values stay selected: `Graphics card` keeps the chosen detected GPU while `Runtime GPU` reports the adapter that actually resolved, and the selected window size stays distinct from the fitted window and drawable size. The block contains no user, repository or installation paths. **Detailed logging** is on by default: the full runtime log is kept after every session. Turned off, the run still writes a shorter log while it runs — warnings, errors and the startup lines that identify the port, platform, GPU, render settings and runtime configuration — but a clean exit discards that session's log, so normal sessions do not accumulate files. Closing the window keeps the log: the process ends without shutting down its logging, because game threads may still write to it. The choice in effect at exit decides whether the log is kept. If the game crashes, the log is kept and contains that context plus one `CRASH REPORT` block with uptime, exception details, native frames and any resolvable guest function; no minidump or second diagnostic file is written. Logging and crash reporting are never disabled. **Open logs folder** opens the folder that holds these logs. If a crash occurs, send the newest `hard_corps_uprising_*.log`; for a non-crash problem, turn Detailed logging on, reproduce it, and send the newest log.

## Not offered yet

| Option | Reason |
| --- | --- |
| Frame-rate cap | Earthworm's cap is its own limiter. ReXGlue has no generic one. |
| Language | The data has EN, JP, DE, FR, ES and IT folders, and the game imports `XGetLanguage`, but the mapping from `user_language` is not verified. |
| Deadzone, vibration, stick inversion, button remap | Earthworm-specific guest hooks. |
| Render presets | Earthworm-specific logic over `resolution_scale`. |
| Present effect (FSR/CAS) | Not exposed by Earthworm either. Needs a restart. |
| Achievements, updates, drag and drop of the package | Out of scope. |
