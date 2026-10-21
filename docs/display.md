# Display

## Window

- Default game window: windowed, 853x480 logical, centred on monitor 1 (`config/hard_corps_uprising.toml`). A user's `settings.toml` value replaces it.
- The launcher window is separate: it opens windowed, 1280x720 logical capped to the desktop, on the primary display, whatever the saved game settings are ([launcher.md](launcher.md)). It reopens where it was last left ([launcher.md](launcher.md#window-position)).
- Play starts the game with the saved Window size, Monitor, VSync and Display mode. A new window is needed when the size, monitor or VSync differs from the launcher's, which relaunches the game once. The window is never moved or resized in place: ReXGlue 0.10.0 has no such API ([launcher.md](launcher.md)).
- Guest video mode: 480 lines at the Aspect Ratio's width (853x480 at 16:9), set when the game starts and independent of the window size. Window size is the host window only; the guest picture is scaled into it. Render resolution scales the presented base, which is 720 lines at the Aspect Ratio's width (1280x720 at 16:9) ([launcher.md](launcher.md#graphics)).
- **F11** toggles Borderless Fullscreen. It is SDL's default fullscreen mode, not an exclusive display mode, and it uses the current display. In the launcher, F11 switches the launcher itself instead ([launcher.md](launcher.md#window-position)).
- F11 again returns to windowed. SDL restores the previous windowed size and position.
- Auto-repeat is ignored, so holding F11 toggles once.
- ReXGlue's `Window::SetFullscreen` discards SDL's return value, so a refused switch is not reported.
- F11 switches the window only. It does not change the saved Display mode, so the next start uses the launcher's choice.
- In gameplay the mouse cursor follows the last input device in both Windowed and Borderless Fullscreen: mouse activity shows it, keyboard or controller activity hides it. It stays visible while a mouse-driven overlay is open and while the window has no focus. The launcher is unaffected.

## Title FPS

- Title: `Hard Corps: Uprising Recompiled | FPS: 60.00`, refreshed once per second. Turned off by the launcher's "Frame rate in title" (`hcu_show_fps = false`), which leaves `Hard Corps: Uprising Recompiled`.
- With "Render resolution in title" on (`hcu_show_render_resolution = true`, off by default), the title shows the render resolution between the name and the FPS: `Hard Corps: Uprising Recompiled | 3360x1440 | FPS: 60.00`. That is the Render resolution chosen in Graphics, not the window size or the Internal render resolution.
- Shows the plain title until the first frame is counted.
- Shown in windowed mode. Still counted in fullscreen, where the title bar is hidden.
- Source: the title's `VdSwap` call (hooked in `src/window_host.cpp`), so the value is the number of frames the game swapped per second, whatever the Frame rate setting. A swap the idle-present thread repeats after 30 ms without a new frame is not counted.
- The F2 counter reads the same sample.

## Frame rate

The launcher's Gameplay page has a **Frame rate** (`hcu_frame_rate`): 30, 60 Original, 90, 120, 144, 165, 240 or Unlimited. 60 is the default and the game's own pacing.

- **60 Original** changes nothing in how frames are paced. The low-FPS game speed correction (`hcu_low_fps_game_speed`) still keeps the game at real-time speed when frames miss the 60 Hz deadline.
- **Gameplay simulation always stays at 60 Hz.** The game is a fixed-step engine: one step per main-loop iteration, and its timers, AI, physics and animation state machines count steps. At a higher rate the loop still runs many more times per second, so the stage scene's step accumulator (`sub_824E1590`) is given zero or one step per iteration, and the game functions that count iterations (`sub_82501D28`, `sub_824DC8C0`, `sub_825182D0`) run once per step. Game speed, AI and timers are therefore unchanged. A rate the machine cannot reach, or a monitor and VSync that hold it near 60, simply runs at the original cadence with no interpolation.
- **30** limits the loop to 30 iterations per second and gives the game two steps per iteration, so it runs at normal speed instead of at half speed.
- **Above 60** the present callback (`sub_82551A70`) is told to flip at once instead of at the next 60 Hz guest vblank, and a high-resolution wait before `VdSwap` (`src/frame_limiter.*`, `src/frame_wait.cpp`) holds the selected rate. Unlimited does not wait. VSync keeps working: the presenter still follows the Display page's VSync.
- **Visual interpolation** is off at 60 and 30, always on at 90 to 240, and follows the measured guest render rate at Unlimited: it starts on, turns off after two one-second windows below 63 FPS and on again after two at or above 70.6 FPS. The monitor refresh and VSync do not change it. Between two 60 Hz steps the object translations and the stage camera are drawn between the last two simulation states, and put back when the draw ends, so the simulation never reads a drawn value (`src/visual_interpolation.h`, `src/frame_schedule.cpp`). An object whose rotation or scale changes between two steps, or that is drawn as several parts with different transforms, is drawn as the game built it. A move of more than 8 units, a camera turn of more than 0.25, a missed step, another kind of object at the same address, a new scene or a pause start the track again from the current value. Rotation, scale, particles and effects are not interpolated; they step at 60 Hz.
- The rates other than 60 apply during **stage gameplay only**: from half a second after a stage starts running unpaused until it is paused, left or replaced. Menus, loading, the pause menu and transitions keep the original 60, because the menus count frames and would run fast. The title FPS and the F2 counter therefore read about 60 there.
- The session log records `Runtime frame pacing: frame rate=144 | simulation=60Hz | visual interpolation=active` when gameplay starts at a rate other than 60 and when interpolation turns on or off.
