# Hard Corps: Uprising v0.7.0

This release brings optional high-refresh-rate rendering with smooth motion interpolation, a broad set of GPU performance improvements, and several launcher and reliability upgrades. It's one of the most substantial updates since the port's initial release.

## Highlights

- **High-refresh-rate rendering.** Play at 90, 120, 144, 165, 240, or Unlimited frames per second, with smooth visual interpolation between simulation steps. The game's own 60 Hz simulation and speed are untouched — you get a smoother picture, not a faster game.
- **GPU performance improvements.** The GPU command processing path, D3D12 submission, and render-target handling have all been optimized, reducing overhead and helping frame delivery stay smooth, especially at higher frame-rate settings.
- **Configurable player names.** Set your own display name for Player 1 and Player 2 in the launcher. Your save data stays tied to your account, not the name you choose.
- **Launcher self-update.** The launcher can now check for and install new releases itself, so staying up to date no longer means a manual download every time.
- **Overhauled session logging and crash reporting.** The launcher and the game now share a single, combined log per session, with an optional detailed-logging mode and crash reports written directly into that log — making it far easier to diagnose a problem when one occurs.

## What's New

### Performance

- Reduced CPU overhead in GPU command processing.
- Asynchronous D3D12 submission, with hardened shutdown behavior.
- Reduced shared-memory and render-target overhead, and fewer redundant draw calls.
- Lock-free guest clock reads.

### Graphics & Display

- Simplified graphics card selection: the launcher now lists your actual detected GPUs directly instead of an "Automatic" placeholder.

### Gameplay & High Refresh

- New Frame Rate options in Gameplay settings: 30, 60 (Original), 90, 120, 144, 165, 240, and Unlimited.
- Above 60 FPS, visual interpolation smooths motion between simulation steps without increasing game speed; the simulation itself remains at a fixed 60 Hz.
- Some transitions intentionally snap rather than interpolate when necessary to avoid visual artifacts.
- Game speed below 60 FPS is correctly preserved.

### Controls & Launcher

- Configurable local player names.
- Keyboard remapping, with more reliable control-scheme persistence.
- The title screen now accepts Start from any connected controller.
- Improved gamepad support in overlay screens.
- Input-aware mouse cursor visibility.
- Ctrl+S now saves settings correctly even while typing in a text field.
- Improved high-DPI behavior with PerMonitorV2 awareness.
- New launcher header art and author links.
- Improved launcher diagnostics and log-folder access.

### Stability & Reliability

- Launcher and game now share one combined session log.
- Optional Detailed Logging mode.
- Single-log crash reporting on Windows.
- Improved D3D12 asynchronous-submission shutdown behavior.
- Window close no longer risks the previous shutdown hang.
- Additional v0.7.0 release hardening and runtime validation.

### Linux / AppImage

- Improved handling when no display or Vulkan surface is available at startup.
- AppImage continues to support the extract-and-run fallback.
- Linux release remains compatible with the Ubuntu 20.04 / glibc 2.31 baseline.

## Fixes

- Fixed incorrect game speed when running below 60 FPS.
- Fixed visual deformation that could occur in some character animations when using high-refresh interpolation.
- Fixed a shutdown hang that could occur when closing the game window.
- Fixed control-scheme settings not persisting correctly.
- Fixed the title screen only accepting Start from one specific controller.
- Fixed Ctrl+S not saving while a launcher text field had focus.

## Downloads

- `HardCorpsUprising-v0.7.0-windows-x64.zip`
- `HardCorpsUprisingRecompiled-v0.7.0-x86_64.AppImage`

SHA-256 values will be taken from the final artifacts generated from the commit that receives tag v0.7.0.

## Notes

- Linux remains experimental and has not been broadly validated on real Linux PCs or Steam Deck hardware.
- High refresh rates above 60 FPS apply during stage gameplay. Menus, loading and pause screens retain their original pacing where intended.
- No game files are included. You need your own legally obtained copy of Hard Corps: Uprising.
