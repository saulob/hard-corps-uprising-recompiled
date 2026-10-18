# 20 Source map

Subsystem to implementation files (repository-relative; reusable infrastructure and fixes only). Class tags as in the
[README](README.md#classification). `generated/`, `tools/rexglue/`, `out/`, `build*/` and `dist/` are local
and git-ignored. Test executables are CMake targets `hcu_*` marked `EXCLUDE_FROM_ALL`.

## ReXGlue patches and patched runtime
- `patches/rexglue-sdk/NNNN-*.patch` (37 files, 0020 absent), `patches/rexglue-sdk/baseline.env` (pin and stock hashes): [01](01-rexglue-core.md)
- `tools/rexruntime-patch.ps1` (patch discovery, fingerprint, stamp, check), `tools/build-patched-rexruntime.ps1`, `tools/build-patched-rexruntime.sh`
- `tools/build.ps1`, `tools/build.sh` (rebuild patched runtime when the check fails)
- `CMakeLists.txt`: `hcu_rexglue_check`, `hcu_rexglue_staging`, `LINK_DEPENDS` on SDK libraries, `hcu_enable_thinlto`
- Docs of record in the repo: `docs/rexglue-runtime-patch.md`

## Build, codegen, resources
- `CMakeLists.txt`, `CMakePresets.json`, `hard_corps_uprising_manifest.toml`, `.recomp.json`, `.gitattributes` (line endings)
- `config/hard_corps_uprising.toml` (shipped defaults), `config/overrides.toml`, `config/discovered-functions.toml`
- `tools/codegen.ps1`, `tools/find-missing-functions.py`, `tools/setup-linux-build-env.sh`
- `packaging/windows/hard_corps_uprising.rc`, `packaging/icons/*`, `tools/make-app-icon.py`: [02](02-build-system.md)

## ReXApp integration and launcher
- `src/hard_corps_uprising_app.h`, `src/main.cpp` (lifecycle): [01](01-rexglue-core.md)
- `src/launcher.cpp`, `src/launcher.h`, `src/launcher_achievements.*`, `src/modal_dialog.*`, `src/notice_dialog.*`, `third_party/trg-launcher` (submodule)
- `src/game_files.*` (install, verify, swap, DLC), `src/stfs.*` (STFS reader): [03](03-launcher.md)
- `src/platform.cpp`, `src/platform.h` (Win32/Linux helpers, file chooser, graphics-failure box)
- `src/skip_intro.*`

## Windows
- `src/platform.cpp` (Windows branches), `packaging/windows/hard_corps_uprising.rc`, `patches/rexglue-sdk/0015-*`, `0027-*`, `0011-0014`, `0023`, `0031`: [04](04-platform-windows.md)

## Linux
- `src/platform.cpp` (Linux branches), `packaging/linux/AppRun`, `packaging/linux/*.desktop`, `tools/package-linux.sh`, `tools/setup-linux-build-env.sh`, `docs/linux.md`
- Linux patches: `0016-0019`, `0021`, `0024`, `0032`, `0034`, `0035`: [05](05-platform-linux.md)

## Graphics
- `patches/rexglue-sdk/0026-*`, `0027-*`, `0028-*`, `0034-*`, `0035-*`, `0010-*`, `src/multisampling.h`, `tests/multisampling_tests.cpp`, `0036-*`, `0037-*`, `0038-*`, `src/internal_render_scale.h`, `src/graphics_adapters.*`, `tests/internal_render_scale_tests.cpp`, `tests/graphics_adapter_tests.cpp`: [06](06-graphics-vulkan.md)

## Display, window, frame pacing
- `src/window_host.*` (F11, title, cursor auto-hide), `src/window_placement.h`, `src/frame_counter.*`
- `patches/rexglue-sdk/0025-*`, `0029-*`; `docs/display.md`; tests `tests/window_host_tests.cpp`, `tests/window_placement_tests.cpp`, `tests/frame_counter_tests.cpp`: [07](07-display-window-frame-pacing.md)

## Input
- `src/window_host.*`, `src/hard_corps_uprising_app.h` (input gating, `OnShutdown`), `docs/input.md`: [08](08-input-gamepad-mouse.md)
- `src/imgui_gamepad.*` (`ImGuiGamepad`: the SDL3 controller-to-ImGui bridge for the launcher and the overlays)
- Rumble: none at HEAD; branch-only `investigate/controller-rumble`.

## User data and settings
- `src/user_data.*`, `src/settings.*`, `src/legacy_settings.*`, `docs/user-data.md`, `tests/user_data_tests.cpp`: [10](10-user-data-settings-migration.md)

## Offline services
- `src/live_fix.*`, `src/live_stats.*`, `src/local_stats.*`, `src/achievement_alias.*`, `src/achievement_list.h`, `src/achievements_overlay.*`, `src/marketplace_rules.h`, `docs/offline-live.md`, `tests/live_tests.cpp`: [11](11-offline-services-profile-achievements.md)

## Performance
- `patches/rexglue-sdk/` 0002-0009, 0011-0019, 0021-0025, 0029-0032
- Game hooks (`GAME_ADAPTATION`): `src/gpu_fence_wait.cpp`, `src/crt_native.cpp`: [12](12-performance.md)

## Crash hardening
- `patches/rexglue-sdk/0001-*`, `0033-*`, `0010-*`, `src/hard_corps_uprising_app.h` (`OnShutdown`)
- Tests: `tests/physical_heap_stress.cpp`, `tests/shutdown_order_tests.cpp`, `tests/title_thread_shutdown_tests.cpp`, `tests/renderdoc_detect_tests.cpp` (+ `tests/fake_renderdoc.cpp`): [13](13-crash-hardening-threading-memory.md)

## Packaging and release
- `tools/package-release.ps1`, `tools/test-release-check.ps1`, `tools/package-linux.sh`, `packaging/README.txt`, `packaging/licenses/*`, `packaging/linux/*`, `.github/release-notes*.md`: [14](14-packaging-release.md)

## Game features (patterns only)
- `src/cheats.*`, `src/cheat_menu.*`, `src/achievements_overlay.*`, `src/skip_intro.*`, `src/frame_counter.*`: [16](16-reusable-game-features.md); tests `tests/cheats_tests.cpp`, `tests/skip_intro_tests.cpp`

## Project documents in `docs/` (long form)
`display.md`, `first-boot.md`, `input.md`, `launcher.md`, `linux.md`, `minimal-native-build.md`, `offline-live.md`,
`package-analysis.md`, `rexglue-feasibility.md`, `rexglue-runtime-patch.md`, `user-data.md`. This reference points into
them rather than copying.
