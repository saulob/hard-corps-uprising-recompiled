# 19 Cross-project porting guide

Question answered: *I have a fresh ReXGlue project today. What do I port from Hard Corps first?*
Tags as in the [README](README.md#classification). File locations are in [20](20-source-map.md).
Patch numbers are Hard Corps numbers; the authoritative inventory is [01](01-rexglue-core.md#2-patch-inventory-34-patches).

## Prerequisites
Same pinned ReXGlue (v0.10.0, commit in `patches/rexglue-sdk/baseline.env`), or re-validate every patch with
`git apply --check` against your SDK first; upstream may already contain a fix. Toolchains: [02](02-build-system.md).

## MUST HAVE
Only items whose justification does not depend on the game.

| Component | Source | Depends on | Likely conflicts | Assumptions | Focused validation |
| --- | --- | --- | --- | --- | --- |
| Patched-runtime guard: pin, fingerprint, stamp, stock-hash refusal, CMake check/staging, `LINK_DEPENDS` | `patches/rexglue-sdk/baseline.env`, `tools/rexruntime-patch.ps1`, `tools/build-patched-rexruntime.*`, `tools/build.*`, `CMakeLists.txt` | pinned SDK | different SDK commit; different linked targets | the project links `rexruntime` and `rexgpu-xenos` | check fails on a stock DLL and passes after a patched build |
| `0001` physical heap release order | `patches/rexglue-sdk/0001-*`, `tests/physical_heap_stress.cpp` | guard | may be fixed upstream | none | stress target `hcu_physical_heap_stress` (fails on stock per project docs, not re-run here) |
| `0033` stop title threads + app `OnShutdown` input shutdown (`418174b`) | `0033-*`, `src/hard_corps_uprising_app.h` | guard | touches `KernelState`/`XThread` headers (ABI rule) | exit goes through the SDK teardown | `hcu_title_thread_shutdown_tests`, `hcu_shutdown_order_tests` |
| User-data root decision before first release | `src/user_data.*`, [10](10-user-data-settings-migration.md) | none | data already shipped elsewhere | final root chosen | `hcu_user_data_tests` |

Not MUST HAVE although tempting: every performance patch, the launcher, Linux support.

## HIGH VALUE

| Component | Source | Depends on | Likely conflicts | Assumptions | Focused validation |
| --- | --- | --- | --- | --- | --- |
| `0010` RenderDoc load-only detection | `0010-*`, `tests/renderdoc_detect_tests.cpp` | guard | none known | Linux builds especially (test is Linux) | loader test with fake RenderDoc |
| `0034` SPIR-V `exp_adjust` word 3 (correctness, Linux Vulkan) | `0034-*` | guard | may be fixed upstream | Vulkan; visual effect LIKELY, not confirmed on real Linux hardware, no test | compare brightness with D3D12 |
| `0035` cached Vulkan paint pipeline (presenter performance, not correctness) | `0035-*` | guard | may be fixed upstream (issue 507): check first | Vulkan | frame-time trace without a per-frame GPU wait |
| `0026`/`0027` GPU preference | patches | guard | none | investigate first; relevant on hybrid GPUs; no hybrid-GPU run recorded in repo (missing evidence, not a defect) | laptop run (pending) |
| `0029` UI repaint with guest frames | `0029-*` | guard | adds a last virtual to `Window` | adopt when the app keeps permanent ImGui dialogs or otherwise shows the double present | present count with a dialog open |
| Launcher architecture: deferred guest start, settings model, relaunch rule, separate launcher/game window state | `src/launcher.*`, `src/hard_corps_uprising_app.h`, `src/window_placement.h`, `third_party/trg-launcher` | user data | window creation order, cvar names | TRG submodule | `hcu_window_placement_tests`; manual launcher pass |
| Install architecture: fingerprint, staging, verify, swap with backup, rollback (STFS reader only if the title ships as STFS) | `src/game_files.*`, `src/stfs.*` | launcher | package format | inspect first | `hcu_installer_tests` |
| Linux layer + AppImage (RUNPATH, no bundled Vulkan loader, glibc check) | `src/platform.cpp`, `packaging/linux/*`, `tools/package-linux.sh` | launcher | app id naming | Linux build host; real hardware validation limited, see [05](05-platform-linux.md) | packager checks |
| Release-content scans | `tools/package-release.ps1`, `tools/package-linux.sh` | packagers | different layout | technique only; name list is project-specific | add a forbidden file deliberately |
| Codegen discipline: register indirect-call targets, LF line endings | `tools/find-missing-functions.py`, `.gitattributes`, `config/*` | decrypted image | none | game-specific function lists | codegen warnings |
| Performance groups (investigate first, profile before adopting): command-processor CPU (`0002`, `0003`, `0004`, `0025`, `0006`/`0015` timers, `0019`/`0021` Linux waits) | patches | guard | context drift | measured on your game first; no benchmarks in this repo | per-group profile |

## OPTIONAL

| Component | Notes |
| --- | --- |
| Render-target/EDRAM series `0005`, `0007`, `0008` + backend halves `0011`-`0014` (D3D12) / `0016`-`0018` (Vulkan) | Highest semantic risk, no automated tests; port only when profiling shows ownership transfers dominate, then visual-check |
| Resolve copy / deferred writeback `0022`-`0024`, `0030`-`0032` | Game-pattern dependent; cvars exist to disable |
| `0009` guest fence wake | Do not install automatically; useless without the game-side waiter (`GAME_ADAPTATION`, [12](12-performance.md)) |
| `0028` MSAA host log | Diagnostics; lesson: `native_2x_msaa=false` is not "off" ([06](06-graphics-vulkan.md)) |
| ThinLTO, icon pipeline, input-aware cursor visibility, FPS counter | Pure app infrastructure |
| Offline Live replacement | Only if the game uses Live stats/achievements ([11](11-offline-services-profile-achievements.md)) |
| Cheat/runtime-toggle architecture | Pattern only ([16](16-reusable-game-features.md)) |

## GAME SPECIFIC (do not copy)
Fence-wait and CRT hook addresses (`src/gpu_fence_wait.cpp`, `src/crt_native.cpp`), cheat rules and offsets,
the package fingerprint (size + CRC32) and `license_mask = 1`, profile and achievement schemas, DLC marketplace rules
(`src/marketplace_rules.h`), skip-intro hooks, frame-counter hook address, `config/overrides.toml`,
`config/discovered-functions.toml`. Not at HEAD (experimental, stash or branch only): FPS unlock,
rumble diagnostics, gameplay telemetry ([07](07-display-window-frame-pacing.md), [08](08-input-gamepad-mouse.md)).

## RECOMMENDED PORT ORDER
1. Recompile and boot ([17](17-porting-checklist.md) phases 1-2).
2. Patch guard with an empty series (checks pass on stock files), then `0001` and its stress test.
3. User-data root and log location, before any save exists.
4. ReXApp skeleton using the lifecycle table in [01](01-rexglue-core.md#5-rexapp-lifecycle-integration-srchard_corps_uprising_apph), then launcher and installer.
5. `0033` plus `OnShutdown` input shutdown; repeat start/quit a few times.
6. Graphics correctness and presentation: `0010`, `0034` (Vulkan). Then, only where relevant: `0035` (Vulkan presenter performance), `0026`/`0027` (hybrid GPU), `0029` (permanent ImGui dialogs).
7. Window handling and display options; frame pacing only after identifying the game's time step.
8. Linux layer and AppImage (even if lightly tested, to keep abstractions honest).
9. Performance in groups, measuring each ([12](12-performance.md)).
10. Packaging and scans; release candidate on each OS.
11. Offline services, cheats, other game features last.

## VALIDATION POLICY
During porting run only the check for what changed; the comprehensive regression belongs to the final review of the branch (phase 12 of [17](17-porting-checklist.md)).

| Change | Focused check |
| --- | --- |
| SDK patch or stamp inputs | `git apply --check`, patched-runtime check for the platform, build of affected targets |
| Allocator | `hcu_physical_heap_stress` |
| Presenter / pacing | present-count observation with and without a dialog (no automated test in this repo) |
| Title threads / shutdown order | `hcu_title_thread_shutdown_tests`, `hcu_shutdown_order_tests` |
| RenderDoc / loader | `hcu_renderdoc_detect_tests` (Linux) |
| Launcher / placement | `hcu_window_placement_tests`, `hcu_window_host_tests`, manual launcher pass |
| Install | `hcu_installer_tests` |
| User data | `hcu_user_data_tests` |
| Memory, ABI, hooks, concurrency, save data | the extra focused test for that specific risk, immediately |
| Anything | `git diff --check`, no new warnings in touched targets |

## CROSS-PROJECT COMPARISON (Geometry Wars reference, read-only, not authoritative)
Compared against the sibling `docs/rexglue-reference/` and its `patches/rexglue-sdk/` (details in [01](01-rexglue-core.md#8-cross-project-mapping-to-the-geometry-wars-reference-read-only-comparison)).
- Patch content: 33 of 34 Hard Corps patch files are byte-identical to sibling files under different numbers; `0034` matches the sibling's SPIR-V patch in substance. Numbers do not match in general; the table in 01 is the only mapping.
- Same generic problem, different implementation: the sibling guards patches with a `patches.txt` manifest; Hard Corps uses file-name rules and a fingerprint stamp.
- Present in the sibling reference and not in this repository: a frame limiter and presenter pacing test, a profile-default hook, a bloom fix, a launcher gamepad layer with launcher placement tests (per the sibling's own README and guide; not re-verified).
- Present here and not in the sibling reference (per that reference's table of contents): DLC marketplace install, offline leaderboards/downloads, skip-intro option, Vulkan-unavailable error window, glibc-2.31 AppImage baseline work (unreleased, unverified).
- Candidates for a shared baseline: the patch guard, `0001`, `0033` + input shutdown order, `0010`, `0034`, `0035`, GPU preference, `0029`, verified user-data migration, release scans.
