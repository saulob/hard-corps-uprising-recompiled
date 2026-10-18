# Input

## Path

```
Xbox controller (SDL) -> ReXGlue InputSystem, user 0 -> XamInputGetState -> game
Keyboard (mnk_mode)   -> synthetic device, user 0 ---^
```

- ReXGlue owns the whole path. Hard Corps adds no SDL or XAM code.
- While the launcher (or an in-game overlay) is open it reads the controllers itself through SDL, because ReXGlue's input system only starts with the game ([launcher.md](launcher.md)).
- Connection order 0 becomes guest user 0. The keyboard device is synthetic and also targets user 0, but takes no slot, so the pad keeps slot 0.
- `InputSystem` ORs the buttons and keeps the larger value of each stick axis and trigger. The pad and keyboard work together.

## Xbox controller

| Control | XINPUT bit |
|---|---|
| Menu / Start | `0x0010` |
| Back | `0x0020` |
| A / B / X / Y | `0x1000` / `0x2000` / `0x4000` / `0x8000` |
| D-pad | `0x0001` / `0x0002` / `0x0004` / `0x0008` (up, down, left, right) |
| LB / RB | `0x0100` / `0x0200` |
| Stick clicks | `0x0040` left, `0x0080` right |
| LT / RT | analog bytes at state+6 / state+7 |
| Left / right stick | analog X at state+8 / state+12, Y at state+10 / state+14 |

Standard Xbox 360 layout. Guide is ignored (`guide_button` is off).

## Keyboard

Plain arrow keys are the right stick, not menu navigation. Menus use WASD and Shift + Arrow.

The keyboard emulates the pad alongside any connected controller. It is on by default and can be
turned off with **Keyboard** on the launcher's Controls page; the controller keeps working either
way. Every binding below can be changed on that page ([launcher.md](launcher.md)); the table lists
the defaults. The port's own hotkeys (F2, F7, F10, F11, the console) do not depend on this setting.

| Pad control | Keys |
|---|---|
| Start | Enter (Return) or X |
| Back | Z or Tab |
| Left stick | W, A, S, D |
| D-pad | Shift + Up, Down, Left, Right |
| Right stick | Up, Down, Left, Right (plain arrows) |
| A | Space or Semicolon |
| B | Backspace or Quote |
| X | L |
| Y | P |
| LB / RB | 1 / 3 |
| LT / RT | Q or I / E or O |
| Left / right stick click | F / K |

These are ReXGlue's default `keybind_*` values, the same as Earthworm Jim HD. The launcher
writes the saved bindings back as `keybind_*`.

Quick menu check: Enter, WASD, Shift + Arrow, Space, Backspace.

## Host shortcuts

- **F10** opens and closes the cheat menu ([launcher.md](launcher.md#in-game-cheat-menu)). It is handled by a window input listener (`src/cheat_menu.cpp`) above the ImGui drawer, so a press of F10 (and of Escape while the menu is open) is consumed there. While the menu is open the game reads no keyboard or controller input. Auto-repeat is ignored.
- **F11** toggles Windowed and Borderless Fullscreen of the game window. In the launcher it toggles the launcher's own fullscreen. It is handled by a window input listener (`src/window_host.cpp`) above the app's listener, so it is not passed to the game or to the keyboard device. Auto-repeat is ignored. Details are in [display.md](display.md).
- The mouse cursor hides after 5 seconds without movement in Borderless Fullscreen and returns when the mouse moves. It is handled in `src/window_host.cpp` and stays visible while an overlay (F10 menu, Achievements, notice) is open.

## Configuration

`config/hard_corps_uprising.toml`:

- `mnk_mode = true`: the keyboard emulates the pad alongside a connected controller (on by default). Side effect: while on, user 0 always reports connected, even with no pad attached. The launcher's Controls page toggles it; with it off the port's own hotkeys keep working and the saved `keybind_*` bindings are kept.
- The `keybind_*` entries are written to the launcher settings file when a key is rebound, and reset by **Keyboard defaults** on the Controls page. Missing entries fall back to the values in the table above.
- `hid_mappings_file = ""`: the optional `gamecontrollerdb.txt` load is skipped. SDL's built-in mappings still apply. No database is shipped.

## Guest state the game reads

The input consumer is `sub_826692F0`:

- Buttons: u16 big-endian at state+4. START is `& 0x0010`.
- LT / RT: non-zero means pressed. The game maps them to internal bits `0x0400` and `0x0800`.
- Sticks: s16 at state+8 (LX), +10 (LY), +12 (RX), +14 (RY).
- The packet number at state+0 is not read in that consumer, so the keyboard's per-poll packet increments do not change what it sees.
