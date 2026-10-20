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

- Title: `Hard Corps: Uprising | FPS: 60.00`, refreshed once per second. Turned off by the launcher's "Frame rate in title" (`hcu_show_fps = false`), which leaves `Hard Corps: Uprising`.
- With "Render resolution in title" on (`hcu_show_render_resolution = true`, off by default), the title shows the render resolution between the name and the FPS: `Hard Corps: Uprising | 3360x1440 | FPS: 60.00`. That is the Render resolution chosen in Graphics, not the window size or the Internal render resolution.
- Shows the plain title until the first frame is counted.
- Shown in windowed mode. Still counted in fullscreen, where the title bar is hidden.
- Source: a pass-through hook on the title's swap routine `sub_825520F8`, the same kind of hook Earthworm uses for its FPS counter. The original runs first, unchanged, and the hook only counts the call.
- Limitation: that routine skips its `VdSwap` call while `obj+21748` is zero. The count can therefore exceed the number of frames the GPU presented. Read the value as an approximation of the game's frame rate.
- No frame limit, VSync or timing change.
