# 08 Input: controller, keyboard, mouse, hotkeys, rumble

Scope: how input reaches the guest and the host UI, hotkeys, focus and cursor, lifetime of the input system, and the full rumble history. Display and cursor-hide mechanics are in [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md); the launcher's own controller navigation is in [03-launcher.md](03-launcher.md); shutdown ordering in general is in [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md).

Tags and statuses follow [README.md](README.md): `GENERIC_REXGLUE`, `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC`; `CONFIRMED` / `LIKELY` / `UNCERTAIN`; validation `automated` / `manual` / `inferred` / `pending`.

## 0. Status summary

| Item | Label |
| --- | --- |
| Guest input path through ReXGlue (controller + keyboard emulation), shipped config | HEAD IMPLEMENTATION (the SDK does it; the project only sets `mnk_mode`, `hid_mappings_file`) |
| Input shuts down before the window is destroyed (`418174b`) | HEAD IMPLEMENTATION |
| Hotkeys F2, F7, F10, F11, Escape handling; input gating for overlays; launcher gamepad helper | HEAD IMPLEMENTATION |
| Rumble | **No rumble implementation exists in HEAD.** Only a diagnostic exists, on branch `investigate/controller-rumble` (`66e229d`): TEMPORARY DIAGNOSTICS. See section 6 |
| Deadzone, stick inversion, button remap, key rebinding | not offered (`docs/launcher.md`, "Not offered yet") |

Branch note (CONFIRMED with `git merge-base --is-ancestor`): `investigate/controller-rumble` is **not** merged into HEAD. Do not describe any rumble support as shipping.

## 1. Guest input path (HEAD IMPLEMENTATION)

```
controller (SDK driver)  -> rex::input::InputSystem, guest user 0 -> XamInputGetState -> game
keyboard (mnk_mode)      -> synthetic device, guest user 0 --------^
```

- Hard Corps adds no SDL or XAM input code for gameplay. ReXGlue owns the path (`docs/input.md`). The project's input-specific files are the overlay gating callback, hotkey listeners, and the launcher gamepad helper (section 3).
- Config in `config/hard_corps_uprising.toml`: `mnk_mode = true` (keyboard emulates the pad next to a connected controller; side effect, user 0 always reports connected even without a pad) and `hid_mappings_file = ""` (skip the optional `gamecontrollerdb.txt` load; SDL built-in mappings apply). The launcher's Controls page sets `mnk_mode` (on by default) and writes the `keybind_*` bindings.
- Slot assignment: device ordinal 0 becomes guest user 0; the keyboard device is synthetic, targets user 0 but takes no slot, so the pad keeps slot 0. `InputSystem` merges by OR-ing buttons and taking the larger value of each axis and trigger, so pad and keyboard work together.
- Classification: `GENERIC_REXGLUE` (stock SDK behavior). Porting: **adopt directly**; the only decision is `mnk_mode`.
- Which controller driver is active is the SDK's decision. The SDK header documents `CreateDefaultInputSystem` as "SDL + NOP drivers" with an MNK driver for the keyboard and an XInput driver class also present. `docs/input.md` says the pad goes through SDL. The rumble diagnostic patch (section 6) edits `xinput_input_driver.cpp` and prints a hard-coded `backend=xinput`. These do not agree and the repo does not contain a log proving which driver serves the Windows pad. Treat the Windows backend as UNCERTAIN (verify with an SDK build log or by reading `CreateDefaultInputSystem` in the pinned SDK before writing driver-specific code).

### 1.1 Control map and what the game reads

The default `keybind_*` map (X360 pad on keyboard) is ReXGlue's, identical to Earthworm Jim HD: plain arrow keys are the **right stick**, so menus use WASD and Shift+Arrow for the D-pad. Full table in `docs/input.md`. The game's input consumer reads the buttons as a big-endian u16 at `state+4`, LT/RT as nonzero bytes, sticks as s16 at `+8..+14`, and does not read the packet number at `state+0` (so the keyboard's per-poll packet increments do not matter). That consumer address (`sub_826692F0`) is `HARD_CORPS_SPECIFIC`; the XInput state layout is generic.

Quick manual menu check used in this project: Enter, WASD, Shift+Arrow, Space, Backspace.

### 1.2 Windows and Linux differences (modifier limits)

- Shift+Arrow is the D-pad on the keyboard device. A chord depends on the keyboard hardware: many keyboards cannot report certain Shift+Arrow combinations together with other held keys (ghosting). This is a keyboard matrix property and is why the controller path is the primary one; no repo test covers it (pending).
- Holding **Shift at start** shows the launcher. On Windows `trg::ShiftHeld` reads it immediately; on Linux SDL knows the keyboard only once the window has focus, so the launcher appears only if Shift is still held when focus arrives (`docs/linux.md`). `ShiftHeld()` in `src/platform.cpp` does `SDL_PumpEvents` then `SDL_GetModState`. Classification: `PLATFORM_LINUX` limitation, CONFIRMED.
- Steam Deck: the AppImage is meant to be added as a Non-Steam game; the launcher accepts touch/trackpad and controller (`docs/linux.md`). Not tested on real hardware per the docs (validation pending).

## 2. Hotkeys

| Key | Handler | Where | Notes |
| --- | --- | --- | --- |
| F2 | `rex::ui::RegisterBind("bind_hcu_frame_counter")` | `src/frame_counter.cpp` | session toggle of the frame counter; a ReXGlue keybind like F3/F4; not passed to the game |
| F3 / F4 | ReXGlue overlays | SDK | stats overlay and settings overlay; F4 "Save to config" is why the cheat menu does not write cvars (see 16) |
| F7 | `HardCorpsUprisingApp::CreateAchievementsOverlay` override | `src/hard_corps_uprising_app.h` | toggles the project's own overlay instead of the SDK's |
| F10 | `CheatMenu::OnKeyDown` | `src/cheat_menu.cpp` | opens/closes the cheat menu; consumed at z-order 1000 |
| F11 | `WindowHost::OnKeyDown` | `src/window_host.cpp` | borderless fullscreen toggle (game) or launcher fullscreen (launcher) |
| Escape | `EscapeKey` listener | `src/modal_dialog.cpp` | closes the open modal dialog; if none is open it is not handled by the project |

Design rules used (REUSABLE_INFRASTRUCTURE, CONFIRMED):
- Window input listeners are ordered by z-order; the project's use 1000, above the ReXApp listener (0) and the ImGui drawer (64), so the key is consumed before the guest or drawer sees it. All of them ignore auto-repeat through `KeyEvent::prev_state()`.
- Choose keys that the SDK, the game's keyboard map and the launcher do not use (F10 was chosen for this reason; `docs/launcher.md`).
- Porting: **adopt directly** (a handful of lines per hotkey).

## 3. Launcher versus game navigation, gamepad into ImGui

- ReXGlue's input system cannot drive the launcher or an overlay: it starts only with the game, and while an overlay is
  open the project's own active callback makes its driver report a neutral controller. ReXGlue's public
  `InputSystem::GetState()` is therefore the wrong tool here, and the project reads the controllers itself. It does so
  on every platform through one shared implementation, `hcu::ImGuiGamepad` (`src/imgui_gamepad.h`,
  `src/imgui_gamepad.cpp`), on SDL3: `SDL_GetGamepads` / `SDL_OpenGamepad` and `SDL_GetGamepadButton/Axis`, mapped to
  `ImGuiKey_Gamepad*` events and released when the dialog closes.
- **One SDL implementation on every platform.** On Windows the exe links its own static SDL (the runtime DLL embeds
  another copy), so this is a second, independent SDL instance with its own event queue, drained with
  `SDL_PollEvent`. On Linux the program and the runtime share one SDL, whose queue also carries the window's events,
  so only the joystick event range is taken with `SDL_PeepEvents`. `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` is started on
  demand and quit in `Release`; SDL counts subsystem starts, so the game's own input is unaffected.
- **Any pad SDL recognizes**, so a Steam Controller, a DualSense or an Xbox pad all work: this is why XInput is gone,
  as a Steam Controller is a gamepad to SDL but not an XInput slot. Every connected pad is read and their inputs
  combined, so any of them navigates. Pads added or removed while it is open are followed
  (`SDL_EVENT_GAMEPAD_ADDED` / `REMOVED`), and a pad unplugged while a button is held has its key released on the
  next frame.
- Mapping in the launcher: Start plays; A, B, D-pad, left stick navigate.
- Classification: `REUSABLE_INFRASTRUCTURE`, one shared SDL3 implementation for Windows and Linux. Porting: **adopt** if you ship ImGui overlays that must be controllable before or without ReXGlue's input. Pitfall: ImGui's nav flags must be saved and restored around the dialog (`ModalDialog::OnDraw/OnClose` does that).

### 3.1 Modal dialogs and the close gate

`ModalDialog` (`src/modal_dialog.*`, rules in `src/overlay_rules.h`) enforces that a key press used to close or operate an overlay never leaks to the game:
- `CloseGate`: B / Escape / F7 / OK request a close; the dialog closes only after the keys and buttons that went down while it was open are released, or after a 1 second timeout (so a pad unplugged during the dialog cannot block input forever).
- `CheatMenu::GameInputActive` is installed as the input system's **active callback** (`SetActiveCallback`, replacing ReXApp's own, which the cheat menu's version repeats): while any modal is open, or a mouse is over an ImGui overlay, ReXGlue's drivers report an untouched controller, so the game receives nothing.
- Validation: automated in `hcu_live_tests` (overlay rules) and manual in the author's testing; `docs/launcher.md` notes the up-to-one-second hold after close.
- Known quirk (CONFIRMED in the notes): ImGui never closes a modal on Escape by itself, so Escape closing is a project listener and closes a frame late.
- Classification: `REUSABLE_INFRASTRUCTURE`. Porting: **adopt directly** if you use ReXApp ImGui overlays.

## 4. Cursor and focus

- Cursor hide-after-5-seconds only in fullscreen gameplay with focus: `CursorHider` in `src/window_host.h`; details and test in [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md#15-cursor-behaviour).
- Focus loss restores the cursor (`OnLostFocus` -> `UpdateCursor`). Game input while unfocused is the SDK's behavior; the project does not intervene.
- Mouse drives the launcher and the overlays; the game itself does not use the mouse (`mnk_mode` is keyboard emulation only). The F10 menu and Achievements overlay are clickable.
- A pointer over an overlay window (`ImGuiIO::WantCaptureMouse`) also withholds game input, as ReXGlue does for its own overlays.

## 5. Lifetime: input must shut down before the window (`418174b`)

HEAD IMPLEMENTATION, `fix/linux-game-mode-exit-hang`, merged into HEAD ancestry.

- Symptom: Exit Game hang or crash at shutdown, most visible on Linux / SteamOS Game Mode (GitHub issue 9, reporter had not confirmed the fix at the v0.5.0 notes).
- Root cause: `ReXApp` destroys the window before the runtime; the input drivers hold listeners on the window and detached from it **after** it was destroyed (use after free of the destroyed window). The test names the event path: a gamepad button event arriving while the idle loop quits.
- Fix: four lines in `HardCorpsUprisingApp::OnShutdown`: call `runtime()->input_system()->Shutdown()` before anything else, so drivers detach while the window still exists.
- Validation: automated, `hcu_shutdown_order_tests` (`tests/shutdown_order_tests.cpp`): each case runs in a child process with the real UI loop, window and default input system; the "fixed" order is run with and without a pushed `SDL_EVENT_GAMEPAD_BUTTON_DOWN`, and the "stock" order is run but only reported, not counted, because it can hang or crash. Windows' real Exit Game was not automatable in a locked session (noted in earlier work), so the in-game confirmation on Windows is pending.
- Which driver crashed: the repo names the failure as the input drivers detaching from the destroyed window and the project notes name `MnkInputDriver`; the test cannot distinguish drivers, so treat the specific driver as LIKELY.
- Related follow-up (different fix): stopping title threads before teardown, patch 0033 `stop-title-threads` (`abd31d2`), see [01-rexglue-core.md](01-rexglue-core.md) and [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md).
- Classification: `GENERIC_REXGLUE` (any ReXApp subclass that has input drivers and overrides shutdown). Porting: **adopt directly**; high value, tiny cost.

## 6. Rumble: complete history

### 6.1 HEAD

There is **no rumble feature in HEAD**. `grep` finds the word only in `docs/launcher.md` (the "Not offered yet" table: "Deadzone, vibration, stick inversion, button remap | Earthworm-specific guest hooks"), which means the project borrowed the option list from another project and deliberately did not implement it. There is no setting, no hook and no SDK patch for vibration in the 34-patch stack. History search (`git log --all -S rumble`) finds exactly one commit.

### 6.2 `investigate/controller-rumble` (TEMPORARY DIAGNOSTICS)

Commit `66e229d` ("add debug diagnostics for rumble functionality", 2026-10-15), one commit above `a0189f4`/v0.5.0. Contents:

- `patches/rexglue-sdk/0036-debug-saulo-rumble.patch`: logs (deduplicated by last value) in three SDK layers: `InputSystem::SetState` (user, motors, final result, device counts, pad/synthetic), `XinputInputDriver::SetDeviceVibration` (no-slot, skipped, and actual `XInputSetState` result), and `XamInputSetState_entry`.
- `src/debug_saulo_rumble.cpp`: two game hooks. One on the game's motor-set routine logs `user`, `motor`, `strength` (float), the stored left/right 16-bit speeds and the caller address; the other on the game's own `XInputSetState` wrapper logs user/left/right and caller.
- `src/hard_corps_uprising_app.h`: one `[DEBUG_SAULO][Rumble] diagnostic active backend=xinput` line (the unverified backend claim discussed in 1).
- `CMakeLists.txt`: adds the source file.

What it shows (inferred from the code, not from a log in the repo): the game computes a per-motor strength in a game object (motor index 0 or 1, user at an object offset, 16-bit left/right speeds), then calls its XInput wrapper with a pair of 16-bit values, which the SDK forwards to `InputSystem::SetState`; `SetState` returns success if any physical pad rumbled, otherwise the pad error, otherwise success if only synthetic devices exist. The diagnostic exists to find where in that chain the vibration is lost. **The outcome of the investigation is not recorded in the repository** (no log, no notes, no follow-up commit). UNCERTAIN: whether rumble works, is silent, or fails at the driver, the assignment or the game.

Why it must not be merged as is: it carries `DEBUG_SAULO` markers by the project's own convention (temporary, removed before release) and a patch numbered 0036 that would change the patch fingerprint of every runtime build. If rumble is resumed, a clean implementation must start from the findings, not from this patch.

### 6.3 Other places rumble could appear

Checked: the two stashes (`4b3fde6`, `404bc74`) contain no rumble code; the frame-rate stash mentions an XInput read hook only to repeat the last state on frames without a simulation step (EXPERIMENTAL BRANCH, see 07). No telemetry branch commit touches rumble.

### 6.4 Reusable architecture versus Hard Corps specifics

Reusable (as a diagnosis recipe, `REUSABLE_INFRASTRUCTURE`, concept only):
1. Instrument three layers in order: game call, `XamInputSetState` entry, `InputSystem::SetState`, then the driver's `SetDeviceVibration`. Deduplicate on last value so that logs show changes, not per-frame spam.
2. Check slot assignment (`DevicesForUser`) first. With `mnk_mode` the synthetic keyboard device always reports connected; `SetState` with only a synthetic device returns success without rumbling anything.
3. Compare the sum of results: `SetState` returns the pad error only if every physical pad failed.

Hard Corps specific, never generic (`HARD_CORPS_SPECIFIC`): the guest addresses of the motor-set routine and the XInput wrapper, the object offsets of user/left/right, and the assumption of two motors indexed 0 and 1. A port must find its own game's equivalents.

## 7. Pitfalls and checklist

1. Shut down input before the window. Add a child-process test.
2. When `mnk_mode` is on, user 0 is always connected: a game that shows "controller disconnected" prompts never shows them. Decide this deliberately.
3. A launcher that needs a controller before the runtime starts needs its own pad reader (3).
4. Do not assume the driver; confirm in the SDK (1).
5. Keyboard rebinding is not available; the shipped defaults are what the keyboard has.
6. Label rumble work by where it lives. Today: diagnostics only.

## 8. Origin commits

`1f2b0b2` (keyboard and controller input support), `8d6558b` (F11), `dc89a6c` (launcher gamepad), `274ee61` (F2 and overlay close behavior), `28c0251` (F10 cheat menu and active-callback gating), `418174b` (input shutdown order), `66e229d` (rumble diagnostics, branch only). See [18-change-history.md](18-change-history.md).
