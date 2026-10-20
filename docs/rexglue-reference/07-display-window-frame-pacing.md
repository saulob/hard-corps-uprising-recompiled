# 07 Display, window, aspect ratio and frame pacing

Scope: window and display mode handling, presentation, frame pacing, high refresh rate, FPS unlock and aspect ratio / ultrawide, with the history of each. Patch internals are in [01-rexglue-core.md](01-rexglue-core.md); GPU-side work is in [06-graphics-vulkan.md](06-graphics-vulkan.md) and [12-performance.md](12-performance.md); the launcher side of window state is in [03-launcher.md](03-launcher.md).

Tags: classification `GENERIC_REXGLUE`, `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC`; confidence `CONFIRMED` / `LIKELY` / `UNCERTAIN`; validation `automated` (test name), `manual` (repo evidence), `inferred`, `pending`.

## 0. Status of every feature in this file

Read this first. Several features exist only outside the checked-out history, and this document must not be read as saying they ship.

| Feature | Where it lives | Label | Reachable from HEAD? |
| --- | --- | --- | --- |
| Windowed / Borderless Fullscreen, F11, window size, monitor, VSync, launcher relaunch rule | `src/window_host.cpp`, `src/hard_corps_uprising_app.h`, `src/platform.cpp`, `src/launcher.cpp` | HEAD IMPLEMENTATION | yes |
| Input-aware cursor visibility | `src/window_host.*`, `src/cursor_input.h` | HEAD IMPLEMENTATION | yes |
| Saved launcher window placement | `src/window_placement.h`, `src/platform.cpp` | HEAD IMPLEMENTATION | yes |
| Title-bar FPS and F2 frame counter | `src/window_host.cpp`, `src/frame_counter.*` | HEAD IMPLEMENTATION | yes |
| Present-once and vblank-wake pacing | SDK patches 0006, 0015, 0025, 0029 | HEAD IMPLEMENTATION | yes |
| Aspect Ratio: native 16:9 / 16:10 / 21:9 / 32:9 / 4:3 / 5:4 rendering (`hcu_aspect_ratio`) | `src/aspect_ratio.h`, `src/aspect_render.cpp`, `src/launcher.cpp`, `src/settings.cpp` | HEAD IMPLEMENTATION (`release/next`) | yes |
| FPS unlock 120/144 with fixed 60 Hz logic and interpolation | git stash `404bc74` ("WIP experimental high-frame-rate interpolation", base `f3ac390`) plus untracked-files commit `d6ac7ee` | EXPERIMENTAL BRANCH (stash) | no |
| Full-gameplay telemetry (F9 markers, render/window DEBUG_SAULO) | branch `debug/full-gameplay-telemetry` (`6806845`, `4a8f369`, `dd633c8`) | TEMPORARY DIAGNOSTICS | no |

Why the branch name misleads (CONFIRMED, `git merge-base --is-ancestor`): `feature/high-frame-rate` points at `f3ac390` (an offline-achievements commit). It is an ancestor of HEAD, so "merged" is true, but the branch tip does not contain the frame-rate code. That code was stashed with `git stash -u` and exists only as a stash of this local clone. A reader who finds the branch names and assumes "merged feature" is wrong. `perf/stage2-frame-pacing` (`cf6eee1`) is a real merged commit and is HEAD IMPLEMENTATION.

Porting note: stashes are not pushed. Anything below marked EXPERIMENTAL BRANCH can only be recovered from this clone's stash list; the useful design is summarized here so it survives.

## 1. Window and display modes (HEAD IMPLEMENTATION)

Classification: mostly `REUSABLE_INFRASTRUCTURE` for the pattern; the numbers are `HARD_CORPS_SPECIFIC`. Confidence: CONFIRMED. Validation: manual (docs `docs/display.md`, `docs/launcher.md`), automated for pure logic (`hcu_window_host_tests`, `hcu_window_placement_tests`).

### 1.1 Two windows, one rule

The launcher and the game window are separate windows of the same process (the launcher is shown in the pre-game window, `OnFinalizePaths` returns `std::nullopt` until Play calls `resume`). ReXGlue 0.10.0 has no API to resize or move an open window; a window's size, monitor and VSync are fixed at creation.

- Before: launching the game from the launcher either showed a window of the wrong size or required a manual restart.
- Final solution: the launcher compares the pending Window size / Monitor / VSync with what this process created (`GameSettings::WindowChanged`). If any differs, Play relaunches the executable once with `--hcu_skip_launcher=true` (`RelaunchSelf`), and the copy never shows the launcher, so it cannot loop. Display mode alone is applied in place with `Window::SetFullscreen` when the window already matches.
- Files: `src/hard_corps_uprising_app.h` (`OnFinalizePaths`, `callbacks.window_changed`, `callbacks.restart`), `src/settings.cpp` (`GameSettings`), `src/platform.cpp` (`RelaunchSelf`).
- Porting note: **adapt**. Any project whose SDK cannot resize a live window needs this relaunch rule; the specific flag and the Shift-to-show-launcher gesture are the part to copy. Windows shows the launcher with `trg::ShiftHeld`; Linux has to pump SDL first (see section 1.5).
- Side effect: a normal first Play from the launcher costs one process restart. This is intended.

### 1.2 Display mode, F11 and fullscreen

- `fullscreen` setting = start mode. F11 (`WindowHost::OnKeyDown` in `src/window_host.cpp`) toggles `Window::SetFullscreen`, which on SDL is the default **borderless desktop** fullscreen, not exclusive. Auto-repeat is ignored (`prev_state`). The key is consumed by an input listener at z-order 1000, above the app listener (0) and ImGui drawer (64), so it never reaches the guest or the keyboard device.
- F11 does not change the saved Display mode; the launcher's choice wins at the next start.
- In the launcher, F11 switches the launcher window itself and its fullscreen/maximized state is saved (`hcu_launcher_fullscreen`).
- `Window::SetFullscreen` discards SDL's return value, so a refused switch is not reported (CONFIRMED in `docs/display.md`).
- Origin: `8d6558b` (F11 and title FPS), launcher F11 in `e064dd9`.
- Classification: `GENERIC_REXGLUE` pattern (listener above the drawer), `REUSABLE_INFRASTRUCTURE` implementation. Porting: **adopt directly** (needs only `rex::ui::Window`).

### 1.3 Monitor handling

- `monitor = N` opens on `SDL_GetDisplays()[N-1]`; `0` is the primary display. `ListMonitors` in `src/platform.cpp` uses the same order for the launcher list. The order was checked against SDL 3.4.10 source while the SDK ships SDL 3.5.0 whose source was not on disk, so equality for 3.5.0 is LIKELY, not CONFIRMED (stated in `docs/launcher.md`).
- At startup `CheckWindowDisplay` logs a warning when the window did not open on the requested display.
- Window size choices that cannot fit the monitor at its scale are hidden, except the current one.
- No hotplug handling beyond that: a monitor change between runs falls back to defaults for the launcher placement (below); the game window is never moved after creation.

### 1.4 Saved launcher window state (`e064dd9`, branch `feature/window-state-and-cursor`, merged by `d645080`)

- Problem: the launcher always reopened at its default place and size.
- Solution: restored rectangle, maximized flag and fullscreen flag are saved to `settings.toml` (`hcu_launcher_x/y/width/height/maximized/fullscreen`) after the window settles (0.5 s) and again on close. On start, a pure function checks that at least a small corner of the saved rectangle lies on a connected work area (`PlacementIsVisible`); otherwise default placement is used and only the maximized flag is applied.
- Windows places the window before first show with a thread hook (`InstallPreShowPlacement`); Linux places it after it exists, and the desktop may ignore the position (CONFIRMED in `docs/launcher.md`).
- Scope: launcher window only. The game window is never restored from saved placement, because the game relaunch rule fixes its size.
- Validation: automated, `hcu_window_placement_tests` (`tests/window_placement_tests.cpp`).
- Classification: `REUSABLE_INFRASTRUCTURE` (logic in `src/window_placement.h` is header-only and SDK-independent), `PLATFORM_WINDOWS` for the pre-show hook, `PLATFORM_LINUX` for the post-show path. Porting: **adopt directly** for the pure logic, **adapt** the platform calls.

### 1.5 Cursor behaviour

- `CursorHider` follows the last meaningful input device in both Windowed and Borderless Fullscreen: mouse movement,
  a mouse button or the wheel shows the cursor; keyboard or controller activity hides it. Controller activity uses the
  button, D-pad, trigger and stick thresholds in `src/cursor_input.h`; a held input does not keep overriding the mouse.
- Overlays (F10 menu, Achievements, notices) force the cursor visible through `ModalDialog`; runtime ImGui overlays are
  caught by `ImGuiIO::WantCaptureMouse` in a `CursorSync` dialog. Closing an overlay restores the last input device.
  Focus loss shows the cursor; focus return restores the last input device.
- Validation: automated, `hcu_window_host_tests` (state machine and controller filter with synthetic input).
- Classification: `REUSABLE_INFRASTRUCTURE`. Porting: **adopt directly**.

### 1.6 Launcher state versus game state

| Item | Launcher | Game |
| --- | --- | --- |
| Window | 1280x720 logical capped to 90% of the work area, primary display | saved Window size / Monitor, default 853x480 logical on monitor 1 |
| Placement | saved and restored | not saved |
| F11 | launcher fullscreen, saved | window mode of this session, not saved |
| Controller | read directly through SDL gamepads because ReXGlue input starts only with the game | ReXGlue input system |

See [08-input-gamepad-mouse.md](08-input-gamepad-mouse.md) for input and [03-launcher.md](03-launcher.md) for the full table.

## 2. Presentation and the guest video mode

- The guest video mode is set explicitly in `config/hard_corps_uprising.toml` (`video_mode_width = 853`, `video_mode_height = 480`) and does not follow the window size. Without it ReXGlue takes the mode from the window size, which would change what the game believes its display is when the player picks a window size. `HARD_CORPS_SPECIFIC` value, `GENERIC_REXGLUE` lesson: **pin the guest video mode when the window size is a player option**.
- The Xenos plugin presents a 1280x720 base scaled by `resolution_scale`; measured outputs are in `docs/launcher.md` (3840x2160 at scale 3, 5120x2880 at scale 4). Window size scales this picture into the host window.
- The D3D12 presenter always presents with sync interval 0. The launcher's "VSync" is only the tearing permission (`d3d12_allow_variable_refresh_rate_and_tearing` on Windows, `vulkan_allow_present_mode_immediate` on Linux, `kTearingSetting` in `src/settings.h`), inverted for the UI. Real frame pacing comes from the guest's own `vsync` cvar and the guest vblank. CONFIRMED (`docs/launcher.md`). Pitfall: do not describe the launcher VSync as a frame limiter.
- The presenter always letterboxes the format the player chose (`present_letterbox` is forced on and a stored line of it is removed from `settings.toml`). A different window shape therefore shows bars, not a distorted picture. What the format itself renders is section 6.

## 3. Hook used for the frame counter

HEAD counts frames with a pass-through hook on the title's own swap routine (`sub_825520F8` in `src/window_host.cpp`): the original runs first, then the hook counts. Because that routine skips its `VdSwap` call while a game-state field is zero, the count can exceed frames the GPU presented, so the reading is an approximation of the game's frame rate. The measurement changes no timing.

- Classification: `GAME_ADAPTATION` + `HARD_CORPS_SPECIFIC` (the routine address and its gating field). A hook on the `VdSwap` import is the more general technique and is what the experimental branch used (section 5), but even `VdSwap` hooks are not automatically generic: whether `VdSwap` is called once per presented frame is a per-title property.
- Confidence: CONFIRMED that HEAD hooks `sub_825520F8`; CONFIRMED the limitation (documented in `docs/display.md` at `8d6558b`/`274ee61` and the code comment).
- Porting: **concept only** (pass-through counting hook). Do not copy the address.

The frame counter (`src/frame_counter.*`, F2, drawn on the ReXApp-owned ImGui drawer's foreground list, `hcu_show_frame_counter`) formats the same one-second sample as the title ("60 FPS  16.7 ms"). Validation: automated, `hcu_frame_counter_tests`. F2 is a ReXGlue keybind (`rex::ui::RegisterBind`), not saved, and is not passed to the game. Classification: `REUSABLE_INFRASTRUCTURE`. Origin: `274ee61`.

## 4. Frame pacing work that is in HEAD

Mostly SDK patches, so each row links to [01-rexglue-core.md](01-rexglue-core.md) for the inventory. Validation is manual (Windows test system gameplay, `.github/release-notes-v0.4.0.md`, `.github/release-notes-v0.5.0.md`) plus build/package checks; **no frame-time benchmark numbers exist in the repo** beyond the single-system v0.4.0 statement quoted in [12-performance.md](12-performance.md). Quantitative impact is unmeasured.

| Patch | Problem | Change | Class | Confidence | Porting |
| --- | --- | --- | --- | --- | --- |
| 0006 `cpu-timers-and-waits` (vblank part) | the guest vblank thread polled with `Sleep(1)` (about 1.8 ms), so a vblank could arrive up to two milliseconds late | sleeps until the next vblank is due; runs one priority level above normal on Windows only (raising priority needs `CAP_SYS_NICE` on Linux) | `GENERIC_REXGLUE`, `PLATFORM_WINDOWS` for priority | CONFIRMED the code; LIKELY the benefit | investigate first: inspect the relevant part of the pinned SDK and profile CPU waiting / vblank timing; carry only the justified portions; the priority change is Windows-specific |
| 0015 `windows-high-resolution-sleep` | `Sleep` lasted up to a timer tick longer than asked (1 ms took 1.8 ms on the test machine) | high-resolution waitable timer for sleeps of 1 ms and up | `PLATFORM_WINDOWS`, `GENERIC_REXGLUE` | CONFIRMED | investigate first (measure sleep overshoot); Windows only (`nanosleep`) |
| 0025 `wait-reg-mem-vblank-wake` | the game's present waits (`WAIT_REG_MEM`, equal) for a word cleared by the vblank handler; past its yields the command processor polled in 1 ms sleeps, so it resumed up to 1 ms late | vblank thread signals an event after the interrupt; the wait waits on it, bounded by the same 1 ms; other waits keep the poll | `GENERIC_REXGLUE` | CONFIRMED the mechanism; benefit LIKELY | investigate first; only where the title waits on vblank-cleared words |
| 0029 `ui-repaint-with-guest-frames` | while any ImGui dialog exists the presenter paints from the UI thread and the drawer asks for a repaint after every frame, so each guest frame was presented again at the next vblank: about 120 presents per second for 60 guest frames; on Linux with no vblank ticks the repaint ran as fast as the GPU allowed | a UI-only repaint waits for the next guest frame (up to 50 ms, `kUIRepaintGuestOutputWaitNs`) and is presented with it; without guest frames (launcher, stall) it waits for a vblank tick on Windows or the display refresh interval elsewhere; resizes, surface changes and OS repaints still paint at once | `GENERIC_REXGLUE` (any ReXApp that keeps ImGui dialogs alive) | CONFIRMED (patch + release note) | **conditional, high value when the condition applies**: patch generic (`GENERIC_REXGLUE`), bug and mechanism CONFIRMED; verify first that your application keeps permanent ImGui dialogs or otherwise shows the double present, and do not install it automatically. Triggered here because the frame counter and the cursor tick are permanent dialogs |
| 0035 `cache-vulkan-swapchain-paint-pipeline` | Vulkan presenter checked the format of its paint pipeline but never stored it, so every frame it waited for the GPU, destroyed and recreated the pipeline (ReXGlue issue 507) | stores the format; recreates only on a real format change | `PLATFORM_LINUX`, `GENERIC_REXGLUE` | CONFIRMED | high value for Vulkan; check upstream issue 507 / inspect the pinned SDK first and carry it only if the bug is still present; then verify the per-frame GPU wait and pipeline recreation are gone |

Origin of 0025 / 0029: commit `cf6eee1` (`perf/stage2-frame-pacing`); 0035: `3b4a229`. Known failure explained by a superseded approach: the permanent-dialog design (frame counter, cursor tick) is what exposed the double present; v0.4.0 shipped without 0029 and had the doubling (inferred from 0029 being added after the counter in `274ee61`).

Side effects to remember: 0029 makes UI-only repaints depend on guest frames, so an overlay shown while the guest is stalled is repainted by timeout, not instantly; the 50 ms bound exists for that.

## 5. FPS unlock: 120 / 144 FPS with fixed 60 Hz logic (EXPERIMENTAL BRANCH, stash)

Label: **EXPERIMENTAL BRANCH**. Source: stash `404bc74` plus untracked commit `d6ac7ee` on `feature/high-frame-rate`. **Not in HEAD.** HEAD has no frame limiter, no frame-rate setting, and `docs/launcher.md` lists "Frame-rate cap" under "Not offered yet". The Hard Corps-specific design below was runtime-proven on the author's machine per the author's notes, not by an automated test of the game; the pure-logic parts have tests. Quantitative impact: unmeasured.

### 5.1 Facts about the game

- Game logic is a fixed-step: one update advances one 60 Hz step; many counters are frame counted. Therefore simply presenting faster would speed the game up (game-speed is coupled to frame count). The fix is to keep exactly 60 logic steps per second and present extra frames between steps.
- Guest vblank is fixed at the SDK's 60 Hz; the original pacing holds 60 and cannot be raised through a cvar.

### 5.2 Design of the experiment

| Part | What it does | Classification |
| --- | --- | --- |
| `hcu_frame_rate` setting, choices 60 / 120 / 144 (labelled "Experimental" above 60) | stored cvar, unknown values fall back to 60 (`FrameRateOrDefault`) | `REUSABLE_INFRASTRUCTURE` |
| Effective rate (`EffectiveFrameRate(configured, gameplay)`) | rates above 60 apply only during active gameplay, else 60 | `REUSABLE_INFRASTRUCTURE` pattern |
| `GameplayGate` | active after the stage scene has run and ticked unpaused for 30 consecutive iterations (about half a second); leaves at the first iteration without the stage or with a pause | `REUSABLE_INFRASTRUCTURE` logic, state inputs `HARD_CORPS_SPECIFIC` |
| Pause detection | reads the game-state object's world-paused flag, pause-menu flag and the task manager paused flag (offsets +104, +108 and +150 from a fixed global) | `HARD_CORPS_SPECIFIC` addresses, never generic |
| `FrameLimiter` / `WaitUntil` | deadline schedule re-armed on any rate change so catch-up time is never owed; high-resolution timer for most of the wait then a 500 us yield spin (Windows), `sleep_until` plus spin elsewhere | `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS` timer |
| Present mode `kImmediate` | a hook on the title's D3D present callback clears the present-interval bits of its argument so the flip happens immediately instead of at the guest vblank, only for rates above 60 | `GAME_ADAPTATION` / `HARD_CORPS_SPECIFIC` |
| Pacing hook on `VdSwap` import | `PaceGuestFrame()` waits for the next frame time before the SDK's `VdSwap` body; the count also moves here (swap request rate) | `GAME_ADAPTATION` (not generic) |
| Step accounting | the stage scene's step accumulator is pre-set so it owes zero or one step per frame; frame-counted per-frame functions run only on frames that owe a step; the XInput read repeats the last state on frames without a step | `HARD_CORPS_SPECIFIC` |
| Interpolation | camera world matrix, sprite and model object matrices are drawn lerped between the last two simulation ticks, originals restored when the draw ends (`RenderRestore`); a per-tick move above 24 units or a turn above 0.25 is a cut and is not interpolated; tracks forgotten after 60 ticks unseen | pattern `REUSABLE_INFRASTRUCTURE`, offsets `HARD_CORPS_SPECIFIC` |

Known limitations stated by the author: some animations and effects still step at 60 Hz, and other objects are not interpolated. The FPS counter reads about 60 in menus, loading, transitions and the pause menu by design.

### 5.3 Why it was built this way (superseded or rejected ideas explained by evidence)

- Raising only the present rate (with logic still tied to frames) cannot work: logic and draw are in the same main-loop iteration, so frames and steps cannot be separated without the accumulator accounting. Inferred from the shape of the code (step accumulator hooks), not from a documented failed attempt.
- A global rate (not gated by gameplay) was rejected for stability: menus, loading and transitions use the original pacing (`EffectiveFrameRate`, `GameplayGate`). CONFIRMED in the stash docs.
- An fps probe (`src/fps_probe.cpp`, DEBUG_SAULO) existed only while developing and is TEMPORARY DIAGNOSTICS inside the stash.

### 5.4 Validation

- automated (stash only): `hcu_frame_limiter_tests`, `hcu_frame_interpolation_tests`, `hcu_frame_counter_tests`, `hcu_window_host_tests` (frame-rate sampler variant). These cover the schedule, gate, lerp and restore logic without game memory.
- manual: gameplay runs recorded by the author; the 120/144 user test was still **pending** at the time of the last notes. Treat the feature as UNCERTAIN for other users.

### 5.5 Porting notes

- Concept only for Hard Corps addresses. For another title: first prove (a) the logic step is separable from the draw in the main loop, (b) the pause/game-state flags that gate unlocking, (c) that the title's `VdSwap` or present callback can be paced without desyncing audio or timers. If (a) fails, the safe options are 60 only or presenting at monitor refresh with frame doubling.
- Dependencies: 0025 (vblank wake), 0029 (single present) and the high-resolution sleep 0015 are all prerequisites for clean pacing and are already in HEAD.
- Pitfalls: any title with frame-counted timers will drift if their callers run once per presented frame instead of once per step; the 24-unit cut threshold is a per-game guess (LIKELY, tuned to ~0.6 units walking, under 2 dashing).

## 6. Aspect ratio and ultrawide (HEAD IMPLEMENTATION)

Label: **HEAD IMPLEMENTATION** on `release/next`. Replaces the old `present_letterbox` Letterbox/Stretch choice.

### 6.1 What it does

- Setting `hcu_aspect_ratio`: 16:9 (default), 16:10, 21:9, 32:9, 4:3, 5:4 (`src/aspect_ratio.h`). A table maps each format to its guest width at the game's 720-line canvas (1280, 1152, 1680, 2560, 960, 900) and its guest video mode at 480 lines (853, 768, 1120, 1707, 640, 600). Window-size choices in the launcher are filtered by format; changing the format selects that format's default window size.
- Applied in `ApplyAspectRatio()` (`src/aspect_render.cpp`) on the launcher Play path and on the launcher-skipped start: sets `video_mode_width`/`video_mode_height` and `present_letterbox`.
- Game hooks (HARD_CORPS_SPECIFIC, do not reuse addresses): the routine that copies the display-mode struct into the device (writes the width), the routine that reads the display-mode argument and sets the render surfaces (width +56, height +60), and the routine that computes the canvas factor, re-set to `target_ratio / (device_width / device_height)`. The game renders 720 lines at every format and recomputes its own canvas, so the picture is not a stretched 16:9 frame; a wider format shows the game's world further to the sides as far as the game draws it. At 16:9 the hooks do nothing.
- Menus and videos the game composes for 16:9 are not reworked by this feature. The presenter letterboxes the whole picture to the chosen format.
- Render resolution labels in the launcher are per format (`GuestWidth(aspect) * scale`); Internal render resolution derives from the same width.
- Retired setting: the `present_letterbox` line is removed from `settings.toml` by `RemoveSettingLines` (byte-preserving); there is no Stretch mode.

### 6.2 Validation

- automated: `hcu_aspect_ratio_tests` (`tests/aspect_ratio_tests.cpp`: format table, parsing, window sizes, widths and canvas factor, retired-setting removal, setting/launcher/render wiring).
- manual (by the author, real game): 16:9, 21:9 and 4:3 work, also together with Internal render resolution. Not recorded per screen: 16:10, 32:9, 5:4, HUD and menu placement, the pause menu and cutscene video.

### 6.3 Porting notes

- Adapt, investigate first. The reusable idea is a three-part change: (1) guest video mode ratio, (2) the title's render-surface size, (3) the title's own projection/canvas factor, with the presenter letterboxing whatever the guest produces. The first and last are SDK-level; the middle two are always per-title and must be located in that title's display-mode setup code.
- Risks: widening the camera frustum shows level geometry that was never meant to be seen (spawn pop-in, empty edges), UI anchored to the 16:9 canvas, post effects and render targets sized to a hard-coded width, and gameplay advantage in ultrawide.

## 7. High refresh rate and monitor behaviour summary

- 60 Hz game pacing in HEAD; a 144 Hz monitor shows 60 game frames per second (or the host presents repeats where the presenter repaints, which 0029 now limits).
- Variable refresh / tearing is only the VSync toggle above.
- FPS above 60 is EXPERIMENTAL BRANCH (section 5). Higher refresh does not otherwise speed anything up.

## 8. Pitfalls and checklist for a new project

1. Decide whether the guest video mode follows the window size; pin it if the window is a player choice.
2. Prove how the title counts frames before promising an FPS counter or unlock; the swap routine may not call `VdSwap` every iteration.
3. Count presented frames at one place only; permanent ImGui dialogs cause a second present unless 0029-style gating exists.
4. Windowed to fullscreen toggling is cheap (SDL borderless) but resize is not; plan a relaunch.
5. Do not treat a branch name as proof that a feature shipped. In this repository the 120/144 FPS work is a stash although a branch carries its name.

## 9. Origin commits

`8d6558b` (F11, title FPS), `e064dd9` (launcher window state, cursor hide, launcher F11; merge `d645080`), `274ee61` (frame counter, F2), `cf6eee1` (stage 2 frame pacing: 0021-0032, crt natives, ThinLTO), `3b4a229` (0035), stash `404bc74` / `d6ac7ee` (frame-rate experiment), `release/next` (native aspect ratio, internal render resolution, graphics card), `6806845` / `4a8f369` / `dd633c8` (telemetry, TEMPORARY DIAGNOSTICS). Release-wise: v0.3.2 added the window state and counter; v0.5.0 added the pacing patches. Cross-links: [08-input-gamepad-mouse.md](08-input-gamepad-mouse.md), [12-performance.md](12-performance.md), [16-reusable-game-features.md](16-reusable-game-features.md), [18-change-history.md](18-change-history.md).
