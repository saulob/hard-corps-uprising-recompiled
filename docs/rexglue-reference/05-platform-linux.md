# 05 Linux platform

Everything Linux in the Hard Corps: Uprising port: runtime layout, AppImage, build environment, SDK patches that only
Linux needs, and what was and was not tested. Tags: `PLATFORM_LINUX` unless noted. Patch inventory:
[01-rexglue-core.md](01-rexglue-core.md); Vulkan details: [06-graphics-vulkan.md](06-graphics-vulkan.md); Windows
counterpart: [04-platform-windows.md](04-platform-windows.md); packaging flow: [14](14-packaging-release.md).

The Linux build is experimental in the project's own words (`docs/linux.md`, every release note since v0.2.0): a native
x86_64 program on the ReXGlue 0.10.0 Linux runtime with the Xenos plugin's Vulkan backend, shipped as one AppImage.
It is not a Proton or Wine build. Source files: `src/platform.cpp`, `src/window_host.cpp`, `packaging/linux/*`,
`tools/package-linux.sh`, `tools/setup-linux-build-env.sh`, `tools/build.sh`, `tools/build-patched-rexruntime.sh`,
`CMakeLists.txt`, `CMakePresets.json`.

## Tested versus untested

Evidence level is taken from the repo only. "Release notes" means `.github/release-notes-*.md`.

| Area | Status | Evidence |
| --- | --- | --- |
| AppImage builds and passes the AppDir release check | CONFIRMED (manual, released artifacts) | v0.2.0 to v0.5.0 shipped AppImages with `.sha256`; `tools/package-linux.sh` enforces the check |
| Installer / STFS tests on Linux | automated | v0.2.0 notes: "passes its installer tests" (`tests/installer_tests.cpp`) |
| Allocator, shutdown-order, title-thread, RenderDoc-loader tests build on Linux | automated targets exist | `CMakeLists.txt` (`hcu_physical_heap_stress`, `hcu_shutdown_order_tests`, `hcu_title_thread_shutdown_tests`, `hcu_renderdoc_detect_tests`); results are described in `docs/rexglue-runtime-patch.md`, no stored run logs |
| Played on SteamOS by an outside tester | CONFIRMED, limited | notes since v0.3.1: "A SteamOS tester has run it"; issue #6 (RenderDoc overlay, physical heap release errors around exit) and issue #9 (Exit Game hang in Game Mode). Which hardware, which display server and which GPU is not recorded |
| AppImage start when FUSE cannot mount (v0.6.1) | CONFIRMED in a local user-namespace reproducer, UNCERTAIN on SteamOS | `unshare -Ur` fails with the released runtime and starts with the patched one, no variable; cleanup, argument, exit-status and restart cases pass; the reporter's device is untested |
| Fixes for #6 and #9 | UNCERTAIN | v0.5.0 notes: the #9 reporter "has not confirmed the fix yet"; no confirmation of 0010 in the repo |
| Texture brightness fix (0034) | UNCERTAIN on real Linux hardware | v0.5.0 notes: "has not been confirmed on a real Linux PC or Steam Deck yet" |
| Steam Deck | not tested | `docs/linux.md`: "This is the intended flow. It has not been tried on a Steam Deck yet" |
| Rendering, audio, controllers, performance | largely unconfirmed | `docs/linux.md` Known limits |
| X11 vs Wayland vs XWayland | not recorded | the docs list "X11 or Wayland desktop"; no per-server result exists |
| glibc 2.31 baseline (Ubuntu 20.04 build) | build-time only, unreleased | commit `df29fe2` is after the v0.5.0 release; released AppImages say glibc 2.35 |
| Vulkan-unavailable error window | Xvfb, no Vulkan ICD, zenity installed | window visible at 1 s and up for 40 s, click closes it with exit 1; fix not committed |
| WSL as build host | documented | `docs/linux.md`, `tools/package-linux.sh` header ("Run on Linux or WSL"); the repo holds no WSL run log |
| Performance parity with Windows | claimed for the CPU/runtime work only | commit `dc6feb8`; no Linux numbers in the repo (see [12](12-performance.md)) |

Do not describe any item marked UNCERTAIN or "not tested" as validated on real hardware.

## Process layout and library resolution

- **Files.** The AppDir holds `usr/bin/hard_corps_uprising`, `librexruntime.so`, `librexgpu-xenos.so` (the GPU plugin is
  its own library on Linux too), `hard_corps_uprising.toml`, a private `libstdc++.so.6` and `libgcc_s.so.1` in `usr/lib`,
  licenses, icons, `AppRun` and the `.desktop` file. The exact file list is enforced by `package-linux.sh`.
- **RUNPATH.** The game links with `$ORIGIN` (the packaging check requires it in `readelf -d`). CMake only stages the
  runtime beside the program on Windows and macOS, so a `POST_BUILD` `copy_if_different` does it on Linux
  (`CMakeLists.txt`). The patched runtime and plugin are built with the stock RUNPATH `$ORIGIN:$ORIGIN/../lib`
  (`LINK_RPATH` in `tools/build-patched-rexruntime.sh`, `CMAKE_SKIP_RPATH=ON`, then verified with `readelf`), so a
  build folder path never ends up in a library.
- **Stale library prevention.** Same idea as Windows: `LINK_DEPENDS` on `lib/librexruntime*.so` and
  `librexgpu-xenos*.so` in `tools/rexglue/linux-amd64`, plus the `hcu_rexglue_check` / `hcu_rexglue_staging` targets
  (`--check`, `--staged`). See [02](02-build-system.md).
- **C++ runtime.** ReXGlue needs `GLIBCXX_3.4.32` (GCC 13). `packaging/linux/AppRun` looks up the system
  `libstdc++.so.6` (`ldconfig -p`, then fixed paths) and prepends the bundled `usr/lib` to `LD_LIBRARY_PATH` only when
  the system copy lacks that symbol version; a newer system copy must win because graphics drivers may need it
  (comment in `AppRun`). Class: REUSABLE_INFRASTRUCTURE; copy and adapt; the symbol version is project specific.
  Bundled libs carry their copyright file (`libstdc++-copyright.txt`).
- **Not bundled by design.** No SDL shared library (SDL is not a separate file in the AppDir), no X11, Wayland, Vulkan
  loader or driver, no libdecor, no PipeWire. The runtime loads `libX11`, `libX11-xcb`, `libxcb` and
  `libwayland-client` at run time (`docs/linux.md`); SDL loads libdecor and PipeWire when present. The release check
  rejects any file named like RenderDoc or a Vulkan layer or ICD (`*renderdoc*`, `*vklayer*`, `*.json`,
  `implicit_layer.d`, `explicit_layer.d`), any symlink, and any of the three binaries linking RenderDoc. This keeps
  the system's Vulkan loader and driver authoritative.
- **glibc compatibility.** An AppImage needs the glibc of the machine that built it. `MAX_GLIBC=2.31` in
  `package-linux.sh` makes it warn when the host is newer and fail if any packaged binary (including the bundled
  libstdc++ and libgcc) needs a newer symbol version (`readelf --dyn-syms`). Releases up to v0.5.0 were built on Ubuntu
  22.04 (glibc 2.35); `df29fe2` moves the baseline to Ubuntu 20.04 and adds `tools/setup-linux-build-env.sh`. Origin of
  this approach: `df29fe2`; Linux build introduced in `9964f82`.
- **Code generator on the old baseline.** The stock `rexglue` tool needs glibc 2.35, so `package-linux.sh` builds the
  tool from the patched source tree when `rexglue --version` fails, and reads the SDK version from the CMake package
  file instead. Pitfall: the check that used to run the tool would fail on 20.04.

## Build environment

`tools/setup-linux-build-env.sh` (root, Ubuntu 20.04 only; it exits on any other `VERSION_ID`) installs clang 20 from
apt.llvm.org, the GCC 13 runtime from the `ubuntu-toolchain-r` PPA, CMake 3.31.8 (SHA-256 pinned), Meson 1.3.2 and the
SDK's build dependencies (GTK, X11, Xrandr, Vulkan headers, Wayland, ALSA, Pulse and so on). Signing-key fingerprints
for the LLVM and toolchain repositories are verified. libdecor 0.2.2 and PipeWire 0.3.48 are not packaged for 20.04,
so they are built from commits pinned by hash into `/usr/local`; SDL loads them at run time but needs their headers to
build the Wayland-decoration and PipeWire audio backends. Documented recipe: import `ubuntu-base-20.04.5` into WSL (or
use a Docker `ubuntu:20.04`) and run the script as root. `HCU_WORK` (default `~/hcu-linux-build`) selects the
work folder; the script copies sources there because the Linux filesystem is faster and case-sensitive compared with a
Windows drive under `/mnt`. Presets: `linux-base` uses Ninja with `clang-20`/`clang++-20`; builds are ThinLTO with lld
(`-fuse-ld=lld`). `tools/build.sh [release|relwithdebinfo|debug]` is the developer build. `generated/default` comes
from `tools/codegen.ps1` (PowerShell) in the documented flow.

## Window, display and desktop integration

- **Application id.** `SetApplicationId()` sets `SDL_PROP_APP_METADATA_IDENTIFIER_STRING` to
  `io.github.saulob.hard-corps-uprising` before the window exists. Per `docs/linux.md` SDL uses it as the Wayland
  `app_id` and the X11 window class. `packaging/linux/io.github.saulob.hard-corps-uprising.desktop` has the same name and
  `StartupWMClass`; `package-linux.sh` extracts `kApplicationId` from `platform.cpp` with `sed` and fails if the desktop
  file name or the `StartupWMClass` differ, or if the binary does not contain the string. Port rule: one dotted id,
  declared once on one line.
- **Window icon.** SDL does not take the icon from the executable on Linux. `ApplyWindowIcon()` loads
  `../share/icons/hicolor/256x256/apps/hard_corps_uprising.png` relative to the executable folder with `SDL_LoadPNG`
  and sets it on the window, so it works without desktop integration of the AppImage. The AppDir also carries the
  hicolor theme (16 to 512 px and the scalable SVG), the root icon the `.desktop` names, and `.DirIcon`. Origin
  `26bd37c`. Outside an AppImage the icon file is absent and the desktop default stays.
- **No native window handle.** ReXGlue's `GetNativeWindowHandle()` is empty on Linux here, so `platform.cpp` finds the
  launcher window as `SDL_GetWindows()[0]`. Any code ported from the Windows branch that takes an `HWND` needs this
  substitute (`QueryWindowState`, `ApplyWindowPlacement`, `ApplyWindowIcon`).
- **Monitors.** `ListMonitors()` uses `SDL_GetDisplays`, `SDL_GetDisplayBounds`, `SDL_GetDisplayUsableBounds`,
  `SDL_GetDisplayContentScale` and `SDL_GetPrimaryDisplay`. It reports SDL window coordinates, the unit ReXGlue sizes
  windows in (logical size times scale). Because the order is SDL's own, `CheckWindowDisplay` is a no-op.
- **Window placement.** There is no pre-show hook; the launcher moves the window after it exists, and the desktop
  (notably Wayland) may ignore the position (`docs/launcher.md`). Known limit: on Wayland with scaling above 100%
  window sizes can come out larger than chosen.
- **Relaunch.** `RelaunchSelf` reads `/proc/self/cmdline`, adds the extra argument, and `posix_spawn`s. Inside an
  AppImage it starts `$APPIMAGE` again, not `/proc/self/exe`, because the running mount disappears when this process
  exits. Pitfall for ports: the AppImage runtime sets `APPIMAGE`; it is also what `PortFolder()` uses. The command is
  built by `MakeRelaunchCommand` (`src/relaunch_command.h`, `tests/relaunch_tests.cpp`): an absolute program path and
  the original arguments, never a shell string, independent of the working directory.
- **Steam Beta and FUSE (v0.6.1, fixed locally, UNCERTAIN on SteamOS).** A SteamOS tester (issue #13) reported that, on
  Steam's Beta client, the AppImage fails at once from Steam in Game Mode and in Desktop Mode and leaves no game log; direct
  launch and the stable client work. The same week ValveSoftware/steam-for-linux#13736 reported non-Steam AppImages failing
  on the Beta with the runtime's "No suitable fusermount binary found on the $PATH" / "Cannot mount AppImage" lines. The
  released type-2 runtime accepts only a setuid-root `fusermount` found on `PATH`; inside a user namespace it shows as
  owned by `nobody`, which reproduces those lines with this project's AppImage (`unshare -Ur`), and it exits before
  `AppRun`. The AppImage now carries a patched runtime that unpacks and runs itself when the mount fails
  ([14](14-packaging-release.md) section 3.4); `unshare -Ur ./App.AppImage` then starts the game without any variable, and
  `APPIMAGE_EXTRACT_AND_RUN=1` still works. A restart goes through `$APPIMAGE` again and the fallback again. That the
  reporter's failure is this one is not confirmed; if the fallback is not enough, the unpacking can be forced from Steam's
  Launch Options for comparison.
- **Shift to open the launcher.** Windows reads `GetAsyncKeyState`; on Linux `ShiftHeld()` pumps SDL events and reads
  `SDL_GetModState`, which is only valid after a window has focus. `ShiftHeldAfterWindowOpened()` therefore checks again
  once the window exists. Documented limitation: hold Shift until the launcher appears. This is the only Linux input
  or modifier-key limitation recorded in the repo; `docs/input.md` has no Linux-specific section and the keyboard bindings
  are ReXGlue's defaults ([08](08-input-gamepad-mouse.md)).
- **Launcher controller.** `LauncherGamepad` uses SDL: it starts `SDL_INIT_GAMEPAD` on demand, opens the first gamepad,
  maps the SDL buttons to ImGui keys, and quits the subsystem in `Release`. SDL counts subsystem starts, so the
  game's own input is unaffected. Stick dead zone 7849, XInput's left-stick value.
- **Fonts.** `FindUiFonts()` walks a fixed list of Noto Sans, DejaVu Sans and Liberation Sans paths under
  `/usr/share/fonts` and uses the first family whose regular face exists. When none exists it returns an empty set;
  what the launcher then draws is outside what this repo shows (UNCERTAIN).
- **Desktop helpers.** TRG Launcher (a submodule) opens file pickers with `zenity --file-selection`, falling back to
  `kdialog`; if neither exists the call returns empty. Folder and URL buttons run `xdg-open`. `docs/linux.md` lists
  `zenity` or `kdialog` and `xdg-open` as requirements. Whether those exist on a Steam Deck is not recorded.
- **Vulkan unavailable.** `HardCorpsUprisingApp::SetupPresentation` calls `RunLauncherWithoutGraphics()` when ReXGlue's
  setup fails: a 640x400 SDL launcher window (software surface, no GPU, app icon, game files and graphics status, Play and
  Quit; Enter plays, the close button or Alt+F4 quits) so the application still opens. The real launcher is ImGui drawn through the
  Vulkan presenter, so it cannot run here. Play calls `ReportGraphicsFailure()`: a small SDL window (software surface, text from ImGui's built-in font, no GPU) naming Vulkan and pointing at the
  `logs` folder. It closes on a key, a click or the window close button, and the app then exits with a nonzero status.
  It does not use `SDL_ShowSimpleMessageBox`: SDL runs `zenity` first when zenity is installed, and a stalled zenity kept
  the window from appearing. Origin `6cf59bd`. Never run on a machine without Vulkan in the repo evidence (UNCERTAIN).

## Paths, logs and shutdown

- `PortFolder()` returns the folder of `$APPIMAGE` when set, else the executable folder; `game`, `logs` and `user_data`
  live there ([10](10-user-data-settings-migration.md)). An AppImage runs from a read-only mount that vanishes on exit.
- ReXGlue writes logs beside the executable by default; inside an AppImage `OnConfigurePaths` sets the `log_file` cvar
  to `<port folder>/logs/hard_corps_uprising_NNN.log` (next free number from `NextLogFile`), falling back to
  `<user data>/logs`. Only done when the user did not set `log_file`.
- Legacy Linux data was `$XDG_DATA_HOME/hard_corps_uprising`; it is migrated into `user_data` on first start.
- Shutdown on Linux had the repo's worst reported failure: Exit Game hangs and SIGSEGV seconds after "Execution
  complete". Fixes are shared with Windows: `418174b` (input before window), patch 0033 `abd31d2` (stop guest threads),
  patch 0001 (physical heap release order). See [13](13-crash-hardening-threading-memory.md).

## Linux-only and Linux-relevant SDK patches

All live in `patches/rexglue-sdk`; `*.linux.patch` applies only on Linux. Details and ordering: [01](01-rexglue-core.md).

| Patch | What it fixes on Linux | Class | Confidence | Origin |
| --- | --- | --- | --- | --- |
| 0016 `vulkan-submissions-and-occlusion` | Vulkan counterpart of 0003/0011: occlusion queries no longer wait for the whole GPU; submission limit | GENERIC_REXGLUE, perf | CONFIRMED (code) | `dc6feb8` |
| 0017 `vulkan-render-target-transfers` | resolve preflight removed, draw-overwrite cutouts, per-tile stencil bits (matters without `VK_EXT_shader_stencil_export`, e.g. NVIDIA) | GENERIC_REXGLUE, perf | CONFIRMED (code) | `dc6feb8` |
| 0018 `vulkan-edram-dump-dirty-tiles` | dirty-tile EDRAM dumps | GENERIC_REXGLUE, perf | CONFIRMED (code) | `dc6feb8` |
| 0019 `linux-command-thread-wait` | the idle GPU command thread waited with the alertable flag, which on POSIX is polled in 1 ms slices (1000 wakes/s idle) | GENERIC_REXGLUE | CONFIRMED (code) | `dc6feb8` |
| 0021 `posix-wait-multiple-contention` | `WaitMultiple` spun (`yield`) while another thread held a handle's mutex; now sleeps 100 us. Ported from another recompilation project where this loop served the audio worker | GENERIC_REXGLUE | CONFIRMED (code) | `cf6eee1` |
| 0024 `vulkan-resolve-copy-into-textures` | `vkCmdCopyImage` path of 0022 | GENERIC_REXGLUE, perf | CONFIRMED (code) | `cf6eee1` |
| 0032 `vulkan-resolve-deferred-writeback` | Vulkan half of 0030 | GENERIC_REXGLUE, perf | CONFIRMED (code) | `cf6eee1` |
| 0034 `spirv-texture-exp-adjust` | wrong exponent-bias word in the SPIR-V texture fetch: dark images, lost glow | GENERIC_REXGLUE | CONFIRMED (code); effect unconfirmed on real hardware | `430fc32` |
| 0035 `cache-vulkan-swapchain-paint-pipeline` | presenter recreated its paint pipeline after a GPU wait on every frame (ReXGlue issue #507) | GENERIC_REXGLUE | CONFIRMED (code) | `3b4a229` |

Common patches whose Linux motivation was reported or measured on Linux:

- 0001 physical heap release order: reported from SteamOS (issue #6), same code on Windows.
- 0010 RenderDoc detect loaded only: see below.
- 0009 guest fence wake: `futex(FUTEX_WAKE_PRIVATE)` here, `WakeByAddressAll` on Windows; `PhysicalHostOffset` is zero on
  Linux.
- 0026 Vulkan device preference ([06](06-graphics-vulkan.md)).
- 0033 stop title threads: the SIGSEGV after "Execution complete" was observed on Linux under CPU load.
- 0029 UI repaint with guest frames: on Linux, where no vertical-blank tick exists, the UI repaint ran as fast as the GPU
  allowed before the patch (`docs/rexglue-runtime-patch.md`).
- Not needed on Linux: 0015 (`nanosleep` is precise), the vblank priority raise in 0006 (needs `CAP_SYS_NICE`).

## RenderDoc

- **Problem (GENERIC_REXGLUE, CONFIRMED in code and by a Linux test).** `RenderDocAPI::CreateIfConnected` looked up an
  already-attached RenderDoc with `DynamicLibrary::Load` (`dlopen`), which also loads `librenderdoc.so` when the loader
  can find it, and loading it in-process activates RenderDoc. The SteamOS reporter (issue #6) got "RenderDoc API
  initialized" and RenderDoc's overlay without attaching anything.
- **Fix (0010).** New `DynamicLibrary::LoadExisting`: `dlopen(RTLD_NOLOAD | RTLD_LAZY)` on POSIX,
  `GetModuleHandleExW` without the unchanged-refcount flag on Windows. `CreateIfConnected` uses it, so RenderDoc is used
  only when the process already has it. macOS still returns null earlier. Origin `3020c32`.
- **Test.** `tests/renderdoc_detect_tests.cpp` (target `hcu_renderdoc_detect_tests`, Linux only) with
  `tests/fake_renderdoc.cpp`: the fake library sits in `renderdoc/` outside the `$ORIGIN` RUNPATH, the test re-execs
  itself with that folder on `LD_LIBRARY_PATH` so a plain `dlopen` would find it, and checks that nothing is loaded
  unless the library was loaded on purpose and that it unloads with its last owner.
  `build-patched-rexruntime.sh --loader-test` also runs it against the stock runtime, which must fail.
- **Packaging guard.** The AppDir check rejects RenderDoc-named files and any binary linking RenderDoc.
- Porting: copy directly; the pattern applies to any project that ships a ReXGlue runtime.

## Environment variables seen in the repo

| Variable | Used by | Purpose |
| --- | --- | --- |
| `APPIMAGE` | `PortFolder()`, `RelaunchSelf()` | path of the AppImage file (its folder is the port folder); relaunch target |
| `APPIMAGE_EXTRACT_AND_RUN` | the AppImage runtime | unpack to `/tmp` instead of mounting through FUSE (the patched runtime does this by itself when the mount fails) |
| `LD_LIBRARY_PATH` | `AppRun`; the RenderDoc test | prepend bundled `usr/lib`; steer the loader in the test |
| `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT`, `__NV_PRIME_RENDER_OFFLOAD` | patch 0026 | when set, the runtime does not apply its discrete-GPU preference |
| `XDG_DATA_HOME` | ReXGlue default, legacy migration | pre-v0.3.2 data location |
| `HCU_WORK` | `package-linux.sh` | build work folder |
| `SDL_VIDEO_DRIVER=offscreen`, Xvfb | header of `tests/shutdown_order_tests.cpp` | run UI tests without a display |
| `HCU_RENDERDOC_TEST_CHILD` | renderdoc test | child-process marker |

## Pitfalls

- Building on a newer distro raises the AppImage's glibc floor; the script only warns about the host but fails on the
  packaged binaries' actual symbol versions.
- Missing `zenity` and `kdialog` silently disables the package picker.
- AppImage relaunch must use `$APPIMAGE`; using `/proc/self/exe` breaks after the first process exits.
- A Vulkan layer or RenderDoc file in the AppDir is a release blocker by design.
- Do not edit patch files without letting the fingerprint rebuild the Linux runtime; a stale `.so` next to the program is
  the failure the stamp prevents.

## How to verify (focused)

1. `readelf -d usr/bin/hard_corps_uprising` shows RPATH/RUNPATH `$ORIGIN`; `ldd` shows no `not found`.
2. `package-linux.sh` ends with `clean` and per-binary glibc lines at or below 2.31.
3. Log shows `Vulkan device: <name> (type N)` (patch 0026) and the host MSAA line (0028).
4. Window icon and taskbar grouping match the application id under the target desktop.
5. Exit Game three or more times under CPU load: no hang and no SIGSEGV after "Execution complete".
6. With RenderDoc not attached, no RenderDoc overlay and no "RenderDoc API initialized" line.
