# 02 - Build system

Scope: how this repository builds a ReXGlue native recompilation on Windows and Linux, how it ties the build to a
patched copy of the ReXGlue runtime, and how it prevents building or shipping the wrong (stock, stale or mismatched)
runtime. The patches themselves are inventoried in [01-rexglue-core.md](01-rexglue-core.md); packaging is in
[14-packaging-release.md](14-packaging-release.md); the Linux platform specifics are in
[05-platform-linux.md](05-platform-linux.md).

Tags: GENERIC_REXGLUE, REUSABLE_INFRASTRUCTURE, PLATFORM_WINDOWS, PLATFORM_LINUX, GAME_ADAPTATION,
HARD_CORPS_SPECIFIC. Confidence: CONFIRMED / LIKELY / UNCERTAIN. Validation is stated per item as automated (a test in
`tests/`), manual (repo evidence), inferred or pending. This document was written by reading the scripts and history;
no build was run while writing it.

## 1. Summary

| Topic | Result | Class | Confidence |
| --- | --- | --- | --- |
| SDK source | Stock ReXGlue release zip (pinned version and hash) is downloaded and unpacked into `tools/rexglue/<platform>`; the repo never builds the whole SDK | REUSABLE_INFRASTRUCTURE | CONFIRMED |
| Runtime patching | Only the runtime and the Xenos GPU plugin are rebuilt from a pinned source clone with `patches/rexglue-sdk/*.patch`, then installed over the stock binaries; headers stay stock | GENERIC_REXGLUE (mechanism), HARD_CORPS_SPECIFIC (patch content) | CONFIRMED |
| Staleness control | SHA-256 stamp (`rexruntime-patch.txt`) holding an inputs fingerprint plus per-file hashes, checked before every build, after staging and before packaging | REUSABLE_INFRASTRUCTURE | CONFIRMED |
| Compiler | Clang only (generated code uses GNU extensions); C++23; ThinLTO in optimized configs | GENERIC_REXGLUE | CONFIRMED |
| Linux baseline | Release builds on Ubuntu 20.04 (glibc 2.31) since `df29fe2`; enforced by a symbol-version scan in `tools/package-linux.sh` | PLATFORM_LINUX | CONFIRMED in scripts; building on 20.04 not re-run here (pending) |
| Tests | Standalone `EXCLUDE_FROM_ALL` executables, no CTest | REUSABLE_INFRASTRUCTURE | CONFIRMED |

## 2. Toolchain and platforms

| Item | Windows | Linux |
| --- | --- | --- |
| Compiler | Visual Studio's bundled `clang++` (`VC\Tools\Llvm\x64\bin`), GNU-style driver, `lld-link`; MSVC `cl.exe` is rejected by the generated code (`docs/minimal-native-build.md`) | `clang-20` / `clang++-20`, `lld-20` (preset `linux-base`) |
| C++ library | MSVC STL and Windows SDK (found through `VsDevCmd.bat`) | libstdc++ from GCC 13 or newer (the runtime needs `GLIBCXX_3.4.32`); bundled into the AppImage only as a fallback |
| Generator | Ninja (VS-bundled CMake and Ninja) | Ninja, CMake 3.25+ (3.31.8 installed by `tools/setup-linux-build-env.sh`) |
| Language | C++23 (`CMAKE_CXX_STANDARD 23`); TRG launcher target forced to C++17 | same |
| Architecture | x86-64 only. Presets are `win-amd64-*` and `linux-amd64-*`. The patched runtime is built with `-march=x86-64-v2` on both | same |
| Entry script | `tools/build.ps1` | `tools/build.sh` (dev) or `tools/package-linux.sh` (release) |

Why Clang only: the generated recompiled code uses `__attribute__((alias))`, GNU statement expressions and
`__builtin_bswap*`. (CONFIRMED, `docs/minimal-native-build.md`; first build commit `99dc9f7`.)

Pitfall: `tools/build.ps1` strips every `\msys64\` entry from `PATH` so MSYS2's gcc/cmake/ninja cannot be picked up
(`99dc9f7`); keep an equivalent guard if the host has several toolchains.

## 3. Directory roles

| Path | Role | Tracked |
| --- | --- | --- |
| `CMakeLists.txt`, `CMakePresets.json` | project and presets | yes |
| `generated/rexglue.cmake`, `generated/default/` | SDK-emitted CMake glue and recompiled C++ (about 135 translation units per the Phase 3 record) | no (`/generated/` ignored) |
| `tools/rexglue/<platform>/` | unpacked stock SDK, later overwritten with patched runtime files plus `rexruntime-patch.txt` | no (ignored) |
| `out/rexglue-sdk-src` | shallow clone of the pinned ReXGlue source and its build tree (use a short path on Windows: `-WorkDir`) | no |
| `build/` | `tools/build.ps1` default tree | no |
| `build-release/` | tree used by `tools/package-release.ps1` | no |
| `out/build/<preset>` | preset trees (`tools/build.sh`, direct `cmake --preset`) | no |
| `patches/rexglue-sdk/` | numbered patches plus `baseline.env` | yes |
| `config/`, `hard_corps_uprising_manifest.toml` | codegen inputs and the shipped runtime TOML | yes |
| `third_party/trg-launcher` | git submodule (see [03-launcher.md](03-launcher.md)) | submodule |

## 4. CMake structure

`CMakeLists.txt` is the SDK `rexglue init` template plus project additions. In order:

1. `project(... VERSION x.y.z)`: the version feeds `HCU_VERSION_MAJOR/MINOR/PATCH` (`hcu::PortVersion()`) and is parsed by `tools/package-release.ps1` and
   `tools/package-linux.sh` (regex on the `project(` line, so keep that line's shape). REUSABLE_INFRASTRUCTURE.
2. `HCU_SDK_DIR` selects `tools/rexglue/win-amd64` or `tools/rexglue/linux-amd64` and is appended to
   `CMAKE_PREFIX_PATH`, so `find_package(rexglue 0.10.0)` in the generated `rexglue.cmake` resolves the downloaded SDK.
3. `-fmacro-prefix-map=<source dir>=.` in Release only (added in `b31b959`), so `__FILE__` in log macros does not carry
   the build machine's path. The release scanners then verify it ([14-packaging-release.md](14-packaging-release.md)).
4. `include(generated/rexglue.cmake)`: SDK-owned. Defines `rexglue_setup_target()`, the `<target>_recomp` OBJECT
   library for the generated sources (precompiled header, `-fasync-exceptions` on Windows clang so guest SEH scopes
   trap hardware faults, `-gline-tables-only`), and a `<target>_codegen` custom target that re-runs
   `rexglue codegen` as a build step driven by a depfile. GENERIC_REXGLUE (comes from the SDK, not this repo).
5. TRG launcher: submodule, or `HCU_TRG_LAUNCHER_DIR` for a local checkout. Its ImGui target is aliased to the ImGui
   inside `rex::runtime` (`hcu_imgui` interface library) so there is exactly one ImGui context. REUSABLE_INFRASTRUCTURE
   (see [03-launcher.md](03-launcher.md)). `_CRT_SECURE_NO_WARNINGS` is added for clang++ on the MSVC CRT.
6. `HCU_SOURCES`, the Windows-only `.rc` (section 11), `rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)`.
7. Runtime staging and relink rules (section 8).
8. `hcu_enable_thinlto()` (added in `cf6eee1`): ThinLTO for Release and RelWithDebInfo only. On Windows it passes
   `/opt:lldlto=3` for Release and an LTO cache dir, because `lld-link` otherwise runs the LTO backend at -O2. Debug is
   skipped because `-O0` marks functions `optnone`. Applied to the recompiled OBJECT library, the game and the tests
   that compile game code. GAME_ADAPTATION-neutral; copy as is for any clang ReXGlue project (LIKELY, only measured by
   the project's own perf work, no numbers recorded here).
9. `config/hard_corps_uprising.toml` copied next to the executable (the runtime reads `<exe dir>/<app>.toml`).
10. Test targets (section 12).

Pitfall: the single-config guard. `CMakeLists.txt` raises `FATAL_ERROR` if `CMAKE_BUILD_TYPE` is not
Debug/RelWithDebInfo/Release for a single-config generator, because the runtime check is per configuration.

## 5. Presets and configurations

`CMakePresets.json` (added in `9964f82`): hidden `windows-base` and `linux-base` (Ninja, `binaryDir`
`out/build/<preset>`, compilers `clang`/`clang++` or `clang-20`/`clang++-20`, gated by `hostSystemName`), then
`win-amd64-{debug,release,relwithdebinfo}` and `linux-amd64-{debug,release,relwithdebinfo}`.

Windows presets assume the Visual Studio environment is already loaded; day-to-day Windows builds go through
`tools/build.ps1`, which loads `VsDevCmd.bat` itself and does not use presets (HARD_CORPS_SPECIFIC layout, copyable
approach). Linux builds do use the presets (`tools/build.sh`, `tools/package-linux.sh`).

| Configuration | Runtime DLL / SO | Patched? | Used for |
| --- | --- | --- | --- |
| Debug | `rexruntimed`, `rexgpu-xenosd` | no, stock | local debugging only |
| RelWithDebInfo | `rexruntimerd`, `rexgpu-xenosrd` (+ `TracyClientrd` on Windows) | yes | default of `tools/build.ps1` |
| Release | `rexruntime`, `rexgpu-xenos` | yes | packaging; default of `tools/build.sh` |

Pitfall: a Debug build runs the stock runtime, so none of the runtime patches (for example the `PhysicalHeap` release
order fix or the RenderDoc detection fix) are active there. Do not use Debug to confirm or reproduce a runtime-patch
behavior. (CONFIRMED by `Update-PatchedRuntime` being skipped for Debug in `tools/build.ps1`.)

## 6. ReXGlue SDK: download versus patched runtime

This project does **not** build against a local ReXGlue source tree. `generated/rexglue.cmake` offers `REXSDK_DIR`
(add_subdirectory of a source tree) but the repo never sets it. Instead:

1. `tools/codegen.ps1` downloads `rexglue-sdk-<version>-win-amd64.zip` from the official GitHub release, verifies the
   zip SHA-256, expands it into `tools/rexglue`, checks `rexglue --version`, checks the game XEX SHA-256, then runs
   `rexglue codegen hard_corps_uprising_manifest.toml`. Windows only; PowerShell. (Linux packaging downloads the
   Linux zip itself with its own pinned hash in `tools/package-linux.sh`.)
2. `patches/rexglue-sdk/baseline.env` pins the source: `SDK_VERSION`, `SDK_TAG`, `SDK_COMMIT`, `SDK_REPO`, plus the
   SHA-256 of every stock file that will be replaced (runtime and GPU plugin for each configuration and platform, and
   the Windows import libs). Both platforms' scripts read this file; it is the single source of the pin.
3. `tools/build-patched-rexruntime.{ps1,sh}` shallow-clones the tag, requires `HEAD == SDK_COMMIT`, resets the tree
   (`git checkout -- .`, `git clean -fdq -- src include tests cmake`) so patches left by another branch cannot leak in,
   `git apply --check` then `git apply` each patch in name order, configures the SDK's own preset, builds only the
   targets `rexruntime` and `rexgpu-xenos` for `RelWithDebInfo` and `Release`, and installs the results over the stock
   files in `tools/rexglue/<platform>`.

Consequences and rules (GENERIC_REXGLUE as a technique, CONFIRMED):

- The game compiles against the **stock** SDK headers. A patch must not change the layout of classes the game or
  `rex_app.cpp` uses (new virtual functions go last; new members must not move ones the game reads inline). This rule
  is stated in `docs/rexglue-runtime-patch.md`.
- Windows quirk: git checks the `libmspack` symlinks out as one-line text files; the Windows script replaces them with
  copies of their targets before building (`build-patched-rexruntime.ps1`).
- `-ffile-prefix-map=<src>=.` keeps the build folder out of the library's strings; the scripts then scan the Release
  binaries for the source folder and fail if found.
- A result byte-identical to the stock file is rejected ("the patch was not built in").

Origin: `ab4f7b7` (v0.3.1, first patch 0001 and both scripts); extended by `0ec7ded` (fingerprinting, RelWithDebInfo,
build integration), `dc6feb8` (per-platform patch suffixes, Linux parity), `430fc32` (hardening, CMake targets,
`tools/build.sh`).

### Patch naming and order

`NNNN-name.patch` applies on both platforms; `.windows.patch` / `.linux.patch` only there. Both scripts reject names
that do not match `^[0-9]{4}-[a-z0-9]+(-[a-z0-9]+)*(\.(windows|linux))?\.patch$` and reject duplicate numbers; patches
apply in ordinal name order. Only the current platform's patches enter its fingerprint, so editing a Linux patch does
not stale the Windows runtime. Current stack: 37 files (0001..0038, 0020 absent), 27 apply on Windows and 28 on Linux
(release notes v0.5.0 give the same counts; see [01-rexglue-core.md](01-rexglue-core.md) for content).

History worth knowing: debug patches were once part of the numbered stack (`0006-debug-saulo-gpu-thread-stats` in
`0ec7ded`, renumbered to 0008 in `b681177` and 0010 in `e950ca3`, deleted in `bbd02f2`). Because they were renumbered to
stay last, every debug round shifted a number that other files and docs cite. Number 0020 never appears in this
repository's history (UNCERTAIN why; the lead's brief says it was a removed debug patch, but no commit adds or removes
it). On branch `investigate/controller-rumble` (not merged into HEAD) `66e229d` adds
`0036-debug-saulo-rumble.patch`; that is experimental and would change the fingerprint if applied.

## 7. Code generation

| Item | Detail | Class |
| --- | --- | --- |
| Manifest | `hard_corps_uprising_manifest.toml` (project name, `sdk_version`, `game_root`, entrypoint XEX, `out_directory_path`, `includes`) | GAME_ADAPTATION |
| Overrides | `config/overrides.toml`: hand-written `[functions]` entries (start, end) for indirect-only functions the SDK analysis missed | HARD_CORPS_SPECIFIC (addresses), technique generic |
| Discovered | `config/discovered-functions.toml`: generated by `tools/find-missing-functions.py --apply-high`, second entry of the manifest `includes` | HARD_CORPS_SPECIFIC |
| `tools/codegen.ps1` | fetch SDK, verify, run codegen, tee to `generated/codegen.log` | REUSABLE_INFRASTRUCTURE |
| In-build codegen | `generated/rexglue.cmake` re-runs codegen when depfile inputs change; Ninja runs that step every build even when it writes 0 files | GENERIC_REXGLUE |

`.gitattributes` pins `hard_corps_uprising_manifest.toml` to `eol=lf`, because codegen rewrites the manifest with LF and
a CRLF checkout made git report it modified after every build (`docs/minimal-native-build.md`). GENERIC_REXGLUE;
CONFIRMED. Also pinned LF: `*.sh`, `AppRun`, `.desktop`, and `patches/rexglue-sdk/*` (patches are applied with
`git apply` on both platforms; CRLF would break them).

`tools/find-missing-functions.py` (added with the first-boot work, adapted from other ReXGlue projects as its header
says) finds code reached only through data pointers, vtables or `lis/addi` pairs that ReXGlue v0.10.0 did not register.
It needs `generated/default` and the decrypted image `game/analysis/default.image.bin` (checked by SHA-256), classifies
candidates HIGH/MEDIUM/LOW/EH, applies only HIGH, and has `--verify`. The classification rules and the 13 manual
overrides it replaced are documented in `docs/first-boot.md`. Port note: concept and structure reusable; every address
constant, section range and the image hash are Hard Corps specific. Validation: manual (the doc records that all 13
manual overrides re-classified HIGH with identical ends). It is a diagnostic aid for a missing-function fatal
(`Call to invalid or unregistered function at guest address ...`); see [15-debugging-logging.md](15-debugging-logging.md).

Linux note: `tools/codegen.ps1` is PowerShell only. The Linux packaging script copies `generated/` from the working
tree (produced by a Windows run) and fails if `generated/default/sources.cmake` is absent. It can also build the
`rexglue` tool from the patched source when the stock Linux binary needs a newer glibc than the build machine
(`df29fe2`). Whether Linux-generated code is byte-identical to Windows-generated code was not established (UNCERTAIN).

## 8. Preventing a wrong or unpatched runtime (mandatory section)

### Problem

The patched runtime and GPU plugin are prebuilt files copied into the SDK folder and then copied again next to the
executable. Three different things can go wrong silently: (a) `tools/rexglue` still holds an older or stock runtime
after a patch, baseline or compiler change; (b) the file next to the executable is not the one in `tools/rexglue`
because staging only runs when the program relinks; (c) a package is assembled from the wrong files. Any of them gives a
binary that behaves like an older build, and a Debug build never exercises the patches at all.

### Symptoms and root cause (CONFIRMED from history)

- Behavior of an older runtime after patches changed, with no error. Root cause recorded in `0ec7ded`: `build.ps1`
  never compared the installed runtime with the patch set, and the SDK stages its DLLs only when the executable links,
  so a rebuilt DLL in `tools/rexglue` was not copied beside an already linked executable.
- A stale-hint example from the local `build-error.log` (a hint only): a patch stack changed (a debug patch renamed and a
  new one added), the check reported "patches added/removed or renamed" and rebuilt, and the rebuild then failed to
  compile because a patch used a private member of a class. The check/rebuild path surfaced the problem instead of
  silently shipping the old DLL.

### Final solution: layered defenses

| Layer | Mechanism | Where | Introduced | Validation |
| --- | --- | --- | --- | --- |
| Pin | tag + commit + stock-file SHA-256 in one env file | `patches/rexglue-sdk/baseline.env` | `ab4f7b7` | manual |
| Inputs fingerprint | SHA-256 over SDK version/tag/commit, platform, compiler version line (clang + MSVC toolset on Windows), build flags, targets, configs, linker RUNPATH (Linux), and every applicable patch's name and hash in order | `Get-RuntimeInputsSha256` in `tools/rexruntime-patch.ps1`; `inputs_sha` in `tools/build-patched-rexruntime.sh` | `ab4f7b7` basics, `0ec7ded` full fingerprint, `dc6feb8` Linux parity, `430fc32` strengthened (compiler, RUNPATH) | manual |
| Stamp | `rexruntime-patch.txt` beside the SDK: `inputs_sha256`, `patch_sha256`, patch name list, SHA-256 of each installed file | `Write-RuntimeStamp` / `Read-RuntimeStamp`; `check_stamp` | `0ec7ded` | manual |
| Stamp check | fails on missing stamp, other commit, other inputs, a file not matching the stamp, **or a file equal to the stock hash**; names what changed ("patches added", "patches removed or renamed", "a patch changed", "compiler changed", "baseline or build settings changed") | `Test-PatchedRuntime`; `check_stamp` | `0ec7ded` | manual |
| Auto-rebuild | `build.ps1` calls `Update-PatchedRuntime` before configuring (not for Debug); `build.sh` and `package-linux.sh` run `--check` and rebuild on failure; rebuild is re-checked and aborts on mismatch | `tools/build.ps1`, `tools/build.sh`, `tools/package-linux.sh` | Windows `0ec7ded`; Linux `430fc32` (`build.sh`), `package-linux.sh` earlier | manual |
| Build-time refusal | after building, the script refuses output equal to stock; Release files are scanned for the source/SDK/project path; the GPU plugin must link the runtime and export `rex_gpu_abi_version` and `rex_gpu_create` (Linux) | `build-patched-rexruntime.*` | `ab4f7b7`/`430fc32` | manual |
| Relink trigger | `LINK_DEPENDS` on the SDK's DLLs/SOs so a replaced runtime forces the program to relink and re-run the staging | `CMakeLists.txt` | Windows `0ec7ded`, Linux `dc6feb8` | manual |
| Staging verification (script) | `Sync-StagedRuntime` copies differing DLLs, then `Assert-RuntimeReady` requires the exe and both DLLs next to it to be byte-identical to `tools/rexglue` and the stamp valid; repeated before `-Run` | `tools/build.ps1` | `0ec7ded` | manual |
| Staging verification (CMake) | `hcu_rexglue_check` (a dependency of the game target) runs the check before the program builds; `hcu_rexglue_staging` (`ALL`) copies the runtime and GPU plugin next to the exe and checks the staged folder; so presets and IDEs are covered, not only the scripts | `CMakeLists.txt`, `-Check -Config $<CONFIG>` / `--check ... --config` | `430fc32` | manual |
| Packaging gate | Windows: `Test-PatchedRuntime` plus SHA equality of the two packaged DLLs against `tools/rexglue`; `-SkipBuild` never updates and fails if stale. Linux: `--check` then `--check --staged` against the build folder and again against the AppDir | `tools/package-release.ps1`, `tools/package-linux.sh` | `ab4f7b7`, `430fc32` | manual |
| Behavioral proof | the allocator stress test fails on the stock runtime and passes on the patched one; the RenderDoc detection test runs against both and must fail on stock | `tests/physical_heap_stress.cpp`, `tests/renderdoc_detect_tests.cpp`, `--stress`, `--loader-test` | `ab4f7b7`, `3020c32` | automated, but only for those two patches |

The Windows PowerShell check is invoked with `PSModulePath` unset and the fixed `powershell.exe`, because a parent
`pwsh` (or a Store pwsh) sets a module path that breaks the child (comment in `CMakeLists.txt`; `430fc32`).

### Classification and porting

- Mechanism (stamp + fingerprint + stock-hash refusal + staged-copy equality + relink trigger): REUSABLE_INFRASTRUCTURE,
  CONFIRMED. Adapt: replace `baseline.env` values, the file list (`$RuntimeFiles`, `LIBS`), and the check targets. The
  PowerShell and bash implementations are parallel; keep both in sync if you support both OSes.
- The patch set and its hashes: HARD_CORPS_SPECIFIC, do not copy.
- Dependencies: `git`, network on first run, the SDK build dependencies (Windows: Visual Studio with the LLVM component;
  Linux: see section 10).

### Pitfalls

- Timestamps are not used; the check is content-based. Do not "fix" a failing check by `touch`.
- Adding, renaming, reordering or editing any patch, changing `baseline.env`, the flags, or upgrading the compiler
  invalidates the stamp and the next build rebuilds ReXGlue (minutes, network the first time).
- Debug skips the patched check and uses stock `*d` files; only staged copies are checked there.
- The check script path is part of CMake; moving `tools/` breaks the build rule, not only the scripts.
- `-Restore` (Windows) reinstalls the stock files and deletes the stamp, so the next `build.ps1` re-patches. Do not run
  the builder script "just to see its options": it has side effects (it rebuilds runtime files).

### How to verify

Focused checks for a change in this area: run the check in isolation (`build-patched-rexruntime.ps1 -Check -Config
Release -Staged <dir>` or `build-patched-rexruntime.sh --check <sdk> --config Release --staged <dir>`), confirm it
reports "current" with the expected patch count, then hand-edit nothing and re-run to see the same result. Full
regression (stress test, loader test, package gates) belongs in the final release review only.

### Superseded approach

`ab4f7b7` introduced the stamp with only the SDK commit, the hash of the single patch, per-file hashes and the
stock-hash refusal, checked by `package-release.ps1` and the Linux packaging script. Until `0ec7ded`, `build.ps1`
itself did not run that check, and nothing verified the copies next to the executable. The first design therefore
had no fingerprint of build settings and no staged-copy verification, which is the gap above.

## 9. Windows build details

`tools/build.ps1 [-Clean] [-Run] [-Config Debug|RelWithDebInfo|Release] [-BuildDir <rel>] [-Jobs N]`:

1. Verifies tools exist at fixed Visual Studio paths (the repo hard-codes a VS install path, with `vswhere` used only in
   the runtime builder; HARD_CORPS_SPECIFIC, adapt), verifies `game/extracted/default.xex` SHA-256, and that
   `tools/rexglue` and `generated/default` exist.
2. Runtime check/update (section 8).
3. Imports the x64 environment from `VsDevCmd.bat`, restores the caller's environment in `finally`.
4. Reconfigures only when the cache lacks the requested build type or compiler, then builds.
5. Syncs and asserts the staged DLLs; requires the exe, the TOML and (non-Release) `TracyClient<suffix>.dll`.
6. `-Run` is the only path that launches the game; it passes `--game_data_root` and a build-local `--user_data_root`.

Release builds for shipping go through `tools/package-release.ps1`, which calls `build.ps1 -Config Release -BuildDir
build-release` ([14-packaging-release.md](14-packaging-release.md)).

## 10. Linux build details

| Item | Detail | Origin |
| --- | --- | --- |
| Dev build | `tools/build.sh [release\|relwithdebinfo\|debug]` -> preset `linux-amd64-<name>` in `out/build/` | `430fc32` |
| Staging | the SDK stages the runtime beside the program only on Windows/macOS, so a POST_BUILD `copy_if_different` copies `librexruntime*.so` next to the executable; RPATH is `$ORIGIN` | `9964f82` |
| Build distro | Ubuntu 20.04 (glibc 2.31), prepared by `tools/setup-linux-build-env.sh` (root, network): LLVM 20 and the GCC 13 runtime from signed apt repos (key fingerprints checked), CMake 3.31.8 (SHA-256 checked), Meson, and libdecor 0.2.2 and PipeWire 0.3.48 built from pinned commits because 20.04 does not package them (SDL loads both at run time and only needs headers to build the backends) | `df29fe2` |
| WSL | `docs/linux.md` documents importing the Ubuntu 20.04 base rootfs into WSL (SHA-256 recorded there) and running the setup script; a Docker `ubuntu:20.04` container is said to work the same way (not verified here) | `df29fe2` |
| glibc history | initial AppImage build targeted Ubuntu 22.04 / glibc 2.35 (`9964f82`); changed to 20.04 / 2.31 in `df29fe2`; doc text finished in `f96c66e` | see [14-packaging-release.md](14-packaging-release.md) |
| Work dir | sources are rsynced to `~/hcu-linux-build` (`HCU_WORK`) because the Windows drive under WSL is slow and case-insensitive | `9964f82` |
| Stock `rexglue` | needs glibc 2.35, so on 20.04 `package-linux.sh` builds `rexglue` from the patched source and installs it | `df29fe2` |
| Patched libs | RelWithDebInfo and Release both patched; stock-style RUNPATH `$ORIGIN:$ORIGIN/../lib` (via `CMAKE_SKIP_RPATH=ON` and an explicit linker flag), not the build folder | `430fc32` |

Run the scripts from WSL or Linux only. Pitfalls: `unzip` may be missing (python3 fallback is coded); do not mix
Windows-checkout CRLF into `*.sh` (the repo pins LF); without network the first patched-runtime build cannot fetch the
source.

## 11. Resources and metadata

- `packaging/windows/hard_corps_uprising.rc` is a single line, `1 ICON "../icons/app-icon.ico"`. CMake enables the `RC`
  language, adds the `.rc` to the sources and sets `OBJECT_DEPENDS` on the `.ico`, so icon changes rebuild it. The
  comment explains why: resource 1 is the first icon in the executable, used by Explorer and by SDL's window class.
  Added in `26bd37c`. PLATFORM_WINDOWS, REUSABLE_INFRASTRUCTURE. There is no version-info resource in this `.rc`
  (CONFIRMED: file content).
- `packaging/icons/` holds the SVG source and the committed PNG/ICO renditions; `tools/make-app-icon.py` regenerates
  them (needs Edge or Chrome for rasterization) and refuses to run if the SVG does not use the launcher accent color.
  The generated files are committed so a normal build needs no SVG tooling.
- Linux icon and `.desktop` handling: [14-packaging-release.md](14-packaging-release.md).

## 12. Tests

Test executables are plain programs registered with `add_executable(... EXCLUDE_FROM_ALL ...)`; there is no
`enable_testing`/CTest. Build one with `cmake --build <dir> --target <name>` and run the produced executable. Several
receive `HCU_SRC_DIR` so a test can read source text (guard-style tests).

| Target | Source | Covers (see the owning doc) |
| --- | --- | --- |
| `hcu_installer_tests` | `tests/installer_tests.cpp` | package parsing/installer ([03-launcher.md](03-launcher.md)) |
| `hcu_cheats_tests` | `tests/cheats_tests.cpp` | cheat guards ([16-reusable-game-features.md](16-reusable-game-features.md)) |
| `hcu_frame_counter_tests`, `hcu_window_host_tests`, `hcu_window_placement_tests` | `tests/frame_counter_tests.cpp`, `window_host_tests.cpp`, `window_placement_tests.cpp` | display and window logic ([07-display-window-frame-pacing.md](07-display-window-frame-pacing.md)) |
| `hcu_skip_intro_tests`, `hcu_multisampling_tests` | `tests/skip_intro_tests.cpp`, `multisampling_tests.cpp` | launcher settings |
| `hcu_internal_render_scale_tests`, `hcu_graphics_adapter_tests` | `tests/internal_render_scale_tests.cpp`, `graphics_adapter_tests.cpp` | Internal render resolution and Graphics card settings ([06-graphics-vulkan.md](06-graphics-vulkan.md)) |
| `hcu_live_tests`, `hcu_user_data_tests` | `tests/live_tests.cpp`, `user_data_tests.cpp` | offline services and user data ([10-user-data-settings-migration.md](10-user-data-settings-migration.md), [11-offline-services-profile-achievements.md](11-offline-services-profile-achievements.md)) |
| `hcu_physical_heap_stress` | `tests/physical_heap_stress.cpp` | links `rex::runtime`; must fail on stock, pass on patched ([13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md)) |
| `hcu_shutdown_order_tests`, `hcu_title_thread_shutdown_tests` | `tests/shutdown_order_tests.cpp`, `tests/title_thread_shutdown_tests.cpp` | runtime teardown (patch 0033) |
| `hcu_crash_report_tests`, `hcu_active_log_tests` | `tests/crash_report_tests.cpp`, `tests/active_log_tests.cpp` (plus `src/crash_report.cpp`, `src/active_log.cpp`) | single-log crash reporting: block layout, recursion guard, real child-process exceptions; the active log target ([15-debugging-logging.md](15-debugging-logging.md)) |
| `hcu_renderdoc_detect_tests` (+ `hcu_fake_renderdoc`) | `tests/renderdoc_detect_tests.cpp`, `tests/fake_renderdoc.cpp` | Linux only; patch 0010 |

Conventions: tests that link `rex::runtime` copy the runtime next to themselves with a POST_BUILD step and set
`BUILD_RPATH $ORIGIN` on Linux; the runner scripts (`--stress`, `--loader-test`, `-StressTest`) deliberately run them
beside the installed (patched) runtime rather than the copy left in the build folder, which may be stale. The runtime
build/package scripts are not covered by a unit test; `tools/test-release-check.ps1` covers only the Windows forbidden
file scanner (see [14-packaging-release.md](14-packaging-release.md)). Validation of the build scripts is manual.

## 13. Common failures

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `Missing tools\rexglue (run tools\codegen.ps1 first)` / `generated\default` missing | SDK not unpacked or codegen not run | run `tools/codegen.ps1` |
| `RUNTIME CHECK FAILED`, "does not match the patched build" or "is the stock ReXGlue release file" | stamp stale, or `-Restore` was run | run `tools/build.ps1` (it rebuilds) |
| "patches\rexglue-sdk\NNNN-... no longer applies" | patch drift against the pinned source, or a leftover tree | the builder resets the tree; re-generate the patch from a clean checkout |
| ReXGlue build error naming a private member of a class | a patch in one file references a member another patch (or order) does not expose | patches apply in number order; fix the patch, not the generated tree |
| "TRG Launcher not found" | submodule not initialized | `git submodule update --init third_party/trg-launcher` or set `HCU_TRG_LAUNCHER_DIR` |
| "Set CMAKE_BUILD_TYPE ..." | configured a single-config tree without a type | use the presets or `build.ps1` |
| Linux: runtime check reports clang missing | `clang++-20` not on PATH | run the setup script / install LLVM 20 |
| Linux packaging: library needs glibc newer than 2.31 | built on a newer distro | build on the 20.04 environment |
| Build-time MSVC errors about GNU attributes | `cl.exe` used | use clang (`CMAKE_CXX_COMPILER`) |
| `git status` shows the manifest modified after every build | CRLF checkout | keep `eol=lf` attribute |

## 14. Release builds

Release builds are produced only through the packaging scripts (section 9, section 10) because they run the extra
runtime gates and the release scanners; a hand-run `cmake --build` of a Release preset checks the stamp and staging but
does not scan for forbidden files. See [14-packaging-release.md](14-packaging-release.md).

## 15. Porting checklist for another project

1. Copy the idea of `baseline.env` + a fingerprint + stamp (adapt, do not copy values).
2. Decide which SDK libraries you patch; keep headers stock and mind class layout.
3. Make staging verification part of CMake (check target + staging target), not only a wrapper script.
4. Add `LINK_DEPENDS` on the prebuilt runtime so replacing it relinks.
5. Pin line endings for manifest, shell scripts and patches.
6. Use clang, C++23, ThinLTO for optimized configs; keep Debug stock-runtime behavior documented.
7. Keep debug patches out of the numbered stack in release branches (see [15-debugging-logging.md](15-debugging-logging.md)).

## 16. Unverified in this document

- Whether the 20.04 toolchain environment produces a working AppImage end to end: scripts and docs exist, no build was
  run during this review (pending).
- Linux-vs-Windows codegen output equality (UNCERTAIN).
- Why number 0020 is absent (UNCERTAIN; not in git history).
- Build timings: none recorded; "a few minutes" for the runtime rebuild is the project's own statement.

## 17. Origin commits

`99dc9f7` minimal native build and first `build.ps1`; `e412e6a` Xenos GPU plugin; `b31b959` release packaging and
`fmacro-prefix-map`; `9964f82` Linux presets, `package-linux.sh`, AppRun; `ab4f7b7` first patch, builders, stamp;
`0ec7ded` fingerprint, `build.ps1` auto-update, staged check, Windows `LINK_DEPENDS`; `dc6feb8` per-platform patches,
Linux `LINK_DEPENDS`; `3020c32` RenderDoc test target; `cf6eee1` ThinLTO; `430fc32` CMake check/staging targets,
`build.sh`, Linux RUNPATH and stronger fingerprint; `26bd37c` `.rc` and icons; `df29fe2`, `536dfdf`, `f96c66e` Linux
20.04 baseline and artifact name; `bbd02f2` debug patch removed.
