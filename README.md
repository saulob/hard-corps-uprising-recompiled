<div align="center">

# Hard Corps: Uprising - PC Port

### Hard Corps: Uprising, the Xbox 360 XBLA spin-off of the Contra series, running natively on Windows, and experimentally on Linux and Steam Deck, through static recompilation, with the TRG launcher and modern PC options

[![Latest release](https://img.shields.io/github/v/release/saulob/hard-corps-uprising-recompiled?style=for-the-badge&label=release&color=99db2e&labelColor=07101f&include_prereleases)](https://github.com/saulob/hard-corps-uprising-recompiled/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/hard-corps-uprising-recompiled/total?style=for-the-badge&color=2e7d32&labelColor=07101f)](https://github.com/saulob/hard-corps-uprising-recompiled/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20experimental%20%7C%20Steam%20Deck%20experimental-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original Xbox 360 game code, statically recompiled to native PC code with the <a href="https://github.com/rexglue/rexglue-sdk">ReXGlue SDK</a>. <b>No game files included</b>: bring your own Hard Corps: Uprising Xbox Live Arcade package.</sub>

</div>

<img width="1966" height="1179" alt="title_screen" src="https://github.com/user-attachments/assets/bff13f4f-3a4e-488d-8cf0-1b6cde1c0875" />

## Features

- **Native Aspect Ratio** (v0.6.0): 16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4. The game renders in the chosen format instead of stretching a 16:9 picture, so wider formats can show more of the sides of the scene. Menus and videos made for 16:9 are not redesigned for every format.
- **Render resolution and Internal render resolution**: Render resolution (1x to 4x) sets the target resolution of the picture. Internal render resolution (100% Native, 75% Performance, 50% Low-end) lowers the resolution the GPU draws at before the picture is scaled up to it, trading sharpness for performance on slower GPUs. These are separate from the Window size, which only sets the host window.
- **Graphics card selection** (Windows): Automatic (recommended) or a specific compatible GPU, useful on PCs with integrated and dedicated graphics.
- **Performance work on Windows and Linux**: far less CPU busy-waiting on the GPU and less render-target and EDRAM transfer work (v0.4.0). v0.5.0 adds further frame-pacing and GPU-side optimizations: each game frame is presented once, the present wakes at the vertical blank, and resolves need less GPU work, especially above the Original render resolution. No change to gameplay.
- **High frame rates**: choose 30, 90, 120, 144, 165, 240 or Unlimited next to the original 60 in the launcher's Gameplay page. Gameplay stays at 60 Hz, so game speed does not change, and above 60 the objects and the camera are drawn between simulation steps.
- **Native static recompilation**: the game's own code, not an emulator. Native Windows build, plus an experimental Linux / Steam Deck AppImage
- **TRG launcher** with a game package installer, optional **DLC characters** (Harley Daniels, Sayuri, Leviathan), graphics and display options, and an optional **Skip intro** that starts at the title screen.
- **High-performance GPU by default**: on PCs with integrated and dedicated graphics the game prefers the dedicated GPU. A GPU chosen in Windows' graphics settings, or on Linux with a selection variable such as `DRI_PRIME`, still takes precedence.
- **Cheats**: press **F10** in game for a cheat menu with Infinite Health, Infinite Lives, Rapid Fire, Jump Height 2x, Keep Weapons on Hit, Movement Speed 2x and Super Armor. Changes made in the menu last for the session.
- **In-game FPS counter**: frame rate and frame time in the corner, enabled from the launcher and toggled with **F2**. The frame rate can also be shown in the window title, as a separate option.
- **Offline achievements and local leaderboards**, with no Xbox Live connection.
- **Portable user data**: saves, settings and player data are in a `user_data` folder beside the program.
- **Window handling**: the launcher remembers its position, size, maximized and borderless-fullscreen state. **F11** toggles borderless fullscreen for the launcher and for the game, each independently. During gameplay the mouse cursor follows the last input device: mouse activity shows it, keyboard or controller activity hides it, and mouse-driven overlays always show it.

## What you need

- Windows 10 or 11, 64-bit, and a DirectX 12 graphics card, **or** 64-bit Linux / a Steam Deck with Vulkan (experimental)
- **Your own Hard Corps: Uprising Xbox Live Arcade package**, this exact game version:

| | |
| --- | --- |
| Game | Hard Corps: Uprising, Xbox Live Arcade |
| Title ID | `58410A18` |
| Content type | `000D0000` (Xbox Live Arcade), STFS `LIVE` package |
| Media ID | `3C5663CB` |
| Game version | `0.0.0.3` |
| `default.xex` | 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

The launcher checks the Title ID and content type when you pick the package, and checks `default.xex` after extracting it. Package containers can differ between copies, so `default.xex` is what identifies the version. Any other version is refused, because the port is built for this exact game code.

## Installing

1. Download **HardCorpsUprising-v0.7.0-windows-x64.zip** from the [latest release](https://github.com/saulob/hard-corps-uprising-recompiled/releases/latest) and extract it anywhere you can write to.
2. Run **hard_corps_uprising.exe**. The launcher opens.
3. On the **Play** page click **Install from package...** and pick your package. It is one file with a long hexadecimal name and no extension. On an Xbox 360 drive it is in `Content\0000000000000000\58410A18\000D0000\`.
4. Wait until **Ready to play** appears. The game files (about 1 GB) are copied into the `game` folder next to the program. The package file itself is not copied there.
5. Press **PLAY**.

**No game files are included** in the release.

**Reinstall from package...** repairs the game files and keeps your settings and saves.

### Optional DLC

The DLC characters are Harley Daniels, Sayuri and Leviathan. DLC is optional, and the port does not include or download DLC packages: you need your own Xbox 360 DLC package for each one.

1. Install the game first.
2. Open the **DLC** page (directly after **Play**), click **Install DLC...**, and pick your DLC package. The launcher checks that it is a Hard Corps: Uprising package (Title ID `58410A18`, content type `00000002`, Marketplace). It refuses the game package and other packages.
3. The **DLC** page shows each character as Installed or Not installed. **Remove** on an installed character removes only that package, after a confirmation. The game and the other DLC are not touched, and you can install the package again later from your own copy.

Installed DLC is stored in the `user_data` folder next to the program (see [docs/user-data.md](docs/user-data.md)), not in the game folder. Reinstalling the game does not remove it.

### Linux and Steam Deck

**Experimental.** This is a native Linux build (ReXGlue's patched Linux runtime and Vulkan GPU plugin, with the same performance work as Windows where it applies), not Proton. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck hardware. v0.5.0 fixes Linux/Vulkan texture brightness and glow rendering issues and improves Exit Game stability. v0.5.1 broadens Linux compatibility (the AppImage is built on Ubuntu 20.04 and needs glibc 2.31 or newer) and shows an error window when the Vulkan graphics system cannot start. v0.5.2 makes that error window self-contained (it no longer depends on zenity, so it also appears in minimal X11 environments) and renames the Linux AppImage to Hard Corps: Uprising Recompiled. v0.5.3 opens a software-drawn fallback launcher when Vulkan cannot start and shows the graphics error only when you press Play. v0.6.0 adds the native aspect-ratio and internal-render-resolution support shared with the Windows graphics work, but Linux/Vulkan remains experimental: it builds and passes automated tests, and the new Vulkan reduced-resolution path has not been validated on real Linux GPU hardware or a Steam Deck during this release cycle. v0.6.1 fixes the AppImage not starting where the system cannot mount it with FUSE, as reported for the Steam Beta client on SteamOS: it now unpacks itself and runs from there. This is verified in a test environment that blocks FUSE, not yet on SteamOS. It needs 64-bit x86 Linux, a Vulkan driver and an X11 or Wayland desktop.

1. Download **HardCorpsUprisingRecompiled-v…-x86_64.AppImage** from the release and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it, then install your package from the **Play** page as above. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

Settings and saves are in the `user_data` folder, and logs in `logs`, both next to the AppImage. Requirements, known limits and building the AppImage are in [docs/linux.md](docs/linux.md).

### Windows SmartScreen

Windows may warn the first time you run the program, because the release is not digitally signed. If you downloaded it from this repository's releases page, choose **More info**, then **Run anyway**.

## The launcher

The launcher opens before the game. Your choices are saved to `settings.toml` in the `user_data` folder next to the program. To turn the launcher off, set **Show this launcher** to **Off** on the Play page. Hold **Shift** while starting to bring it back.

### Play
- **Install from package...** with the Title ID and game version checks, and **Reinstall from package...**
- The **Ready to play** status, and the **Show this launcher** setting

### DLC
- Harley Daniels, Sayuri and Leviathan, each shown as **Installed** or **Not installed**
- **Install DLC...** for your own package, and **Remove** for an installed character (see [Optional DLC](#optional-dlc))

### Display
- **Display mode**: Windowed or Borderless Fullscreen. Press **F11** while playing to switch between them.
- **Window size** and **Monitor**. The launcher reopens at its last position and size, maximized or in its own F11 fullscreen if it was. This is separate from the game's Display mode.
- **VSync** on or off
- **Aspect Ratio**: one dropdown on the Display page (16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3, 5:4). It sets the game's target rendering format, and Window size follows it

Window size, monitor, VSync and graphics card take effect when you press **PLAY**. If one differs from the launcher's window, the game restarts once in a new window.

### Graphics
- **Graphics card** (Windows): Automatic (recommended) or a specific GPU
- **Render resolution**: Original (1280×720 at 16:9), 2x (2560×1440, recommended for 1440p), 3x (3840×2160) or 4x (5120×2880)
- **Internal render resolution**: 100% Native, 75% Performance or 50% Low-end. The GPU draws the game at that share of the Render resolution and the picture is scaled up to it, which lowers the GPU work at some cost in sharpness
- **Anti-aliasing**: Off, FXAA or FXAA Extreme
- **Multisampling**: 2x MSAA (default), or 4x MSAA (Compatibility) if 2x shows graphics problems on your GPU or driver
- **Texture filtering**: Game, Off, 2x, 4x, 8x or 16x

### Gameplay
- **Skip intro**: starts directly at the title screen, without the startup logos and intro. Off by default.
- **Frame rate**: 30, 60 Original (default), 90, 120, 144, 165, 240 or Unlimited. Gameplay simulation stays at 60 Hz at every rate: game speed, enemy AI, timers and physics do not change. Above 60 the objects and the camera are drawn between simulation steps (automatic visual interpolation). Menus, loading and pause stay at 60.
- **Frame rate in title**: shows the frame rate in the window title, for example `Hard Corps: Uprising Recompiled | FPS: 60.00`
- **Render resolution in title**: shows the render resolution chosen in Graphics in the window title, for example `Hard Corps: Uprising Recompiled | 3360x1440 | FPS: 60.00`. Off by default.
- **Frame counter**: shows the frame rate and frame time (`60 FPS  16.7 ms`) in the top-right corner, above the cheat menu. Hidden by default. Press **F2** while playing to show or hide it for the session; the saved setting is not changed.

### Cheats
- **Infinite Health**: normal damage no longer lowers a player's health
- **Infinite Lives**: the number of remaining lives no longer goes down
- **Rapid Fire**: weapons fire about twice as fast
- **Jump Height 2x**: jumps reach about twice the normal height
- **Keep Weapons on Hit**: taking normal damage no longer costs the current weapon
- **Movement Speed 2x**: ordinary running and air steering are twice as fast
- **Super Armor**: take damage normally without knockback, hit stun or knockdown

All cheats are off by default and configured on the Cheats page before Play. They apply to every character and to both players in local two-player.

Press **F10** while playing to open the cheat menu and change the cheats for the current session. Up and Down (or the D-pad) select, Enter (or A) toggles, F10, Escape or B closes. Changes made in the game are not saved: the launcher's settings are the same the next time you start.

### Achievements, leaderboards and download content
Everything stays offline: there is no Xbox Live connection, Microsoft sign-in or online player.
- **Achievements**: the menu items and **F7** open a list of the game's 12 achievements (200G) with their icons and gamerscore. B, Escape, F7 or the X button closes it. Unlocks show the usual notification and are kept between runs. The launcher's **Achievements** page shows the same list.
- **Leaderboards**: the main menu and pause menu items open the game's own Leaderboards screen with your own scores only. Finished games are kept in a local file on this PC. There are no Xbox Live rankings, online players or friends: the boards show your row, or nothing if you have not finished a game on that board. Arcade Mode single player is the board tested so far.
- **Download Content**: shows a short note instead of the Xbox Marketplace. Extra characters are installed from the launcher's DLC page.

[docs/offline-live.md](docs/offline-live.md) has the details.

### Controls
- Xbox 360 controllers work without setup
- **Keyboard**: turns keyboard-to-pad gameplay input on or off (on by default). The controller keeps working either way. Every key can be changed on the Controls page; [docs/input.md](docs/input.md) has the default layout.
- Plain arrow keys are the right stick. Menus use **W A S D** or **Shift + arrow** keys. **Keyboard defaults** on the Controls page restores the standard layout.
- **Local two-player** works with two Xbox controllers. The keyboard and a connected controller both drive player 1.
- Online multiplayer is not supported.

### About
- The port version, game version and library versions
- Open the game folder, the saves and settings folder, or the logs folder
- **Show settings file**, and **Reset all settings**

## Screenshots

<img width="1966" height="1172" alt="gameplay" src="https://github.com/user-attachments/assets/6029f8f7-e0a8-49af-ac51-46d6ab54926d" />

<img width="1966" height="1172" alt="dlc_harley" src="https://github.com/user-attachments/assets/ad445921-1b3d-4e4d-bca6-8496c83a0afe" />


## Reporting problems

If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues). Attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program, and say what you were doing when the problem happened. The launcher and the game write to the same log for one application session. After a crash on Windows the log ends with a `CRASH REPORT` section; no separate crash file or minidump is written. For a non-crash problem, turn Detailed logging on in the launcher, reproduce it, and send the newest log.

## Building it yourself

The recompiled C++ is generated from your own game files, so the repository does not contain it. Clone with submodules, so the TRG launcher is included:

```powershell
git clone --recurse-submodules https://github.com/saulob/hard-corps-uprising-recompiled.git
```

In an existing clone, run `git submodule update --init --recursive`.

1. Install Visual Studio with the C++ and LLVM/Clang components. `tools\build.ps1` expects Visual Studio 18 Community at its default location.
2. Extract your package's files into `game\extracted` with an Xbox 360 STFS extractor, so that `game\extracted\default.xex` exists.
3. `.\tools\codegen.ps1` downloads the pinned ReXGlue SDK 0.10.0, checks `default.xex` and generates the C++ into `generated\`.
4. `.\tools\build.ps1` builds `build\hard_corps_uprising.exe`. `.\tools\build.ps1 -Run` also starts it. It first checks that ReXGlue's runtime and Xenos GPU plugin in `tools\rexglue` were built from this project's patches (`patches\rexglue-sdk`) and rebuilds them from the v0.10.0 source when they were not (network access the first time), and afterwards that the DLLs next to the program are those files; it does not start the game otherwise. See [docs/rexglue-runtime-patch.md](docs/rexglue-runtime-patch.md).
5. `.\tools\package-release.ps1` makes the release ZIP in `dist\`. It builds the same way, refuses to package a runtime that does not match the patches, and publishes nothing. Release and RelWithDebInfo builds (the default) link the game and its recompiled code with ThinLTO.

The Linux AppImage is built on Linux or WSL with `tools/package-linux.sh`, after the steps above have generated the C++, and `tools/build.sh` makes a plain Linux build without packaging. See [docs/linux.md](docs/linux.md).

How the launcher, settings and installer work is described in [docs/launcher.md](docs/launcher.md). The other technical notes are in [docs/](docs/).

## Credits

- Hard Corps: Uprising by Arc System Works, published by Konami for Xbox Live Arcade (2011). This is an unofficial fan-made PC port. It is not affiliated with or endorsed by Konami or Arc System Works, and it contains none of their files.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0. The referenced revision contains no explicit license file.
- The launcher and package installer follow the approach of the [Earthworm Jim HD PC port](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled)
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP

## AI disclosure

AI tools were used to assist with code analysis, development, debugging and documentation. Changes were directed, tested and reviewed by the maintainer.

## License

Original code in this project is licensed under the GNU General Public License v2.0 only (GPL-2.0-only). See [LICENSE](LICENSE).

Third-party components retain their respective licenses and notices. See [licenses/ReXGlue-LICENSE.txt](packaging/licenses/ReXGlue-LICENSE.txt) and [licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt).

No game files are included. Hard Corps: Uprising and its assets are not covered by this project's license.
