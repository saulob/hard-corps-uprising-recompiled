# 13 Crash hardening, threading and memory

Everything in this repository that fixed or prevents a crash, hang, race, use-after-free or lifetime error, ordered by how
likely another ReXGlue project meets the same problem. The patch inventory is in [01-rexglue-core.md](01-rexglue-core.md);
this file explains the failures and the sequences of commits that closed them. Tags: classification
`GENERIC_REXGLUE`, `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `GAME_ADAPTATION`,
`HARD_CORPS_SPECIFIC`; confidence `CONFIRMED` / `LIKELY` / `UNCERTAIN`; validation automated / manual / inferred / pending.

Honest scope: this project had only a few externally reported crashes (issue 6, issue 9). Where a fix was shipped without
the reporter confirming it, that is stated. Nothing here is attributed to a user-reported crash without repo evidence.

## 0. The two exit paths (read first)

`ReXApp` (staged `share/rexglue/rex_app.cpp` of the pinned SDK) ends a session in two very different ways:

| Path | Trigger | What runs |
| --- | --- | --- |
| Window close | `ReXApp::OnClosing` | `TerminateTitle`, flush log, `std::_Exit(0)`. No subsystem teardown at all (the comment in the SDK says teardown can deadlock on a host lock held by a straggler thread). |
| Exit Game / guest ends | the main guest thread finishes (`Execution complete`), then the UI loop quits | `OnDestroy`: `OnShutdown` (app hook), overlays and ImGui released, module thread joined, **window destroyed**, **then** `Runtime::Shutdown` via `runtime_.reset()`. |

Consequence: every teardown-order bug in this repository lived on the second path and was invisible when testing with the
window close button. The tests below reproduce the second path explicitly. Classification `GENERIC_REXGLUE`
(SDK behaviour), `CONFIRMED` by reading the staged file.

## 1. Exit Game shutdown: the sequence of fixes (issue 6 and issue 9)

Reports (from the repo's release notes): issue 6, a crash on exit on SteamOS (also a frame-rate overlay request, unrelated);
issue 9, Exit Game hangs and shutdown crashes on SteamOS Game Mode. Four steps, in commit order:

| Step | Commit | Release | What it fixed | Validation |
| --- | --- | --- | --- | --- |
| 1 | `ab4f7b7` | v0.3.1 | Physical heap release order (patch 0001): the logs of issue 6 showed `BaseHeap::Release failed because address is not a region start` | automated `tests/physical_heap_stress.cpp`; was it *the* exit crash: `UNCERTAIN` (notes say not yet re-tested) |
| 2 | `418174b` | v0.5.0 | `OnShutdown` shuts the input system down before ReXApp destroys the window | automated `tests/shutdown_order_tests.cpp` |
| 3 | `abd31d2` | v0.5.0 | Patch 0033: all title guest threads stopped before runtime teardown | automated `tests/title_thread_shutdown_tests.cpp` |
| 4 | none | pending | End-to-end confirmation on SteamOS Game Mode by the reporter | pending (v0.5.0 notes say the reporter has not confirmed, and that the B-button behaviour in the report may have another cause) |

Why separate fixes: each addresses a different object that a different thread used after it was destroyed. Fixing only
one leaves a crash or hang with a different stack, which is easy to misread as "the fix did not work". When triaging an
exit failure, test steps 2 and 3 independently.

### 1.1 Input drivers detach from a destroyed window (`418174b`) -- `REUSABLE_INFRASTRUCTURE` pattern, `CONFIRMED`
- Failure mode: hang or crash during Exit Game, especially Linux (Game Mode).
- Root cause: the SDK's input drivers are owned by the runtime's input system and detach from the window in their
  destructors; the keyboard/mouse driver's destructor calls its `DetachFromWindow`, which uses `window->app_context()` to run
  a synchronous UI-thread call and then removes its listeners from the window pointer it stored. `ReXApp::OnDestroy`
  destroys the window before `Runtime::Shutdown` destroys the input system, so the detach touched a destroyed window.
  (Read in the pinned SDK source; the exact hang-versus-crash outcome is timing dependent.)
- Fix: in `HardCorpsUprisingApp::OnShutdown`, call `runtime()->input_system()->Shutdown()` first; it clears the drivers
  while the window still exists, so the later runtime shutdown finds nothing. The rest of `OnShutdown` detaches the app's own
  window users (escape-key listener, cheat menu, window host).
- Files: `src/hard_corps_uprising_app.h` (`OnShutdown`), test `tests/shutdown_order_tests.cpp`.
- Test design (worth copying): `hcu_shutdown_order_tests` runs the real SDL UI loop, a real window and real input drivers;
  a non-UI thread asks the loop to quit exactly as the module thread does, then performs the `OnDestroy` order. Each case
  runs in a child process (it can hang or crash) with a 15 s limit; cases cover "no pad event" and "pad event" (a pushed
  gamepad button event). Requires a display on Linux (`SDL_VIDEO_DRIVER=offscreen` works per the file header). The stock order
  is run and reported but not counted, so the test shows what the bug looked like.
- Dependencies: none beyond the app. Port: copy the pattern directly into any ReXApp subclass, `GAME_ADAPTATION` free.
- Pitfall: any other object registered on the window (listeners, ImGui dialogs, overlays) has the same problem; detach
  them in `OnShutdown` too. Do not assume `OnShutdown` happens late enough; the runtime is destroyed after the window.

### 1.2 Title threads outlive runtime teardown (patch 0033, `abd31d2`) -- `GENERIC_REXGLUE`, `CONFIRMED`
Mechanism and design are in [01-rexglue-core.md](01-rexglue-core.md#42-stop-title-threads-before-teardown-0033----generic_rexglue-confirmed-mechanism).
Failure-side summary:
- Failure mode: SIGSEGV on Linux a few seconds after `Execution complete`, more likely under CPU load; Game Mode hang.
- Root cause: `KernelState::TerminateTitle` gave other guest threads 200 ms to exit and then dropped the survivors. The
  game's polling worker sleeps 500 ms per round in `KeDelayExecutionThread`; if it was mid-sleep it woke after
  `Runtime::Shutdown` freed kernel state and unmapped guest memory and ran guest code on freed memory.
- Fix: count running guest threads, park every thread that leaves a kernel wait once none may run, never kill, terminate
  the title in `Runtime::Shutdown` if nothing did, and end the process with `_Exit` after 2 s if a thread is stuck in
  guest code. Objects of unfinished threads are retained.
- Why not `TerminateThread`: it orphans whatever host lock the thread holds (CRT heap, mutexes) and deadlocks teardown;
  the SDK comments say the same.
- Validation: `hcu_title_thread_shutdown_tests` (poller in a 500 ms delay, a thread on a cancelled periodic timer, a
  thread running guest code, then a runaway thread that never waits); the poller deliberately falls asleep just before
  termination so its sleep outlasts it. After the runtime is gone, the test waits 1.2 s (longer than the poller's sleep)
  and counts guest rounds.
- Residual risk (`UNCERTAIN`): a game thread blocked in a host wait the patch does not know (a custom hook that blocks
  without `GuestWait`) counts as running, so the 2 s rule exits the process: the symptom is a hard exit after
  `Execution complete` with the log line `XThread::StopTitleThreads: guest threads are still running; exiting`. Whether
  a Hard Corps session has ever hit it is not recorded in the repository. A straggler-versus-teardown crash on threads that
  never reach a counted wait is the known follow-up area.
- Linux vs Windows: platform independent code. A real Windows Exit Game run is not automated; the test uses the runtime
  directly on both platforms.

### 1.3 Practical order to apply when an exit fails (`REUSABLE_INFRASTRUCTURE` lesson)
1. Reproduce through Exit Game, not the window close button (section 0).
2. Look for allocator errors in the log (`BaseHeap::Release failed`): patch 0001.
3. Run the input/window ordering test to rule out detach-after-destroy.
4. Run the title-thread test; check the log for the `StopTitleThreads` exit line.
5. Only then suspect GPU or presenter teardown.

## 2. Memory and heaps

### 2.1 Physical heap release race (patch 0001, `ab4f7b7`) -- `GENERIC_REXGLUE`, `CONFIRMED`
Full write-up in [01 section 4.1](01-rexglue-core.md#41-physical-heap-release-order-0001----generic_rexglue-confirmed).
- Failure signature: `BaseHeap::Release failed because address is not a region start` and `PhysicalHeap::Release failed due to
  parent heap failure` in the log, then undefined behaviour in a page table that no longer matches memory.
- Validation: automated `tests/physical_heap_stress.cpp` (`hcu_physical_heap_stress`, `tools/build-patched-rexruntime.ps1
  -StressTest -SkipBuild` or `tools/build-patched-rexruntime.sh --stress <sdk dir> <build dir>`): fails with the stock
  runtime and passes patched (project documentation; not re-run for this reference).
- Superseded: nothing. The test is the model for any timing-dependent crash: build a deterministic stress test of the
  suspected subsystem before arguing about the cause.
- Pitfall: the test needs the patched runtime installed beside it (CMake copies the runtime next to the test target).

### 2.2 Native memory hooks and unsafe ranges (`src/crt_native.cpp`) -- `GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC` addresses
Not a crash fix but a crash-avoidance rule worth reusing: the native `memcpy`/`memset` hooks decline any range that is not
one host pointer (physical view above `0xE0000000` is offset on some hosts), that crosses into the GPU register window
(plain stores would bypass register side effects), or that overlaps a forward copy. Rule for a new project: before
replacing a guest memory routine with host operations, define which guest ranges are host-contiguous and which are
trap-handled, and fall back to the original function for everything else. Validation: inferred; no test.

### 2.3 Deferred GPU memory writes (patches 0030-0032) -- `GENERIC_REXGLUE`, risk `LIKELY`
With resolution scaling a resolve's memory may be left unwritten while the texture holds the only copy; the textures
cannot be evicted, and the memory is written before any reader. Failure modes if the invariant breaks are stale or
missing data, not crashes. Listed here because it is an ordering invariant between GPU work and guest memory. Validation:
manual only. Disable switch: cvar `resolve_defer_writeback`. See [12](12-performance.md).

## 3. Threading and synchronization

| Item | Patch / file | Failure mode | Class | Validation |
| --- | --- | --- | --- | --- |
| Guest critical section spin | 0006 (`xboxkrnl_rtl.cpp`) | Spinning is allowed only on a free lock without waiters (`lock_count == -1`), so a spinning thread can never overtake a waiter (a fairness hazard, not a crash) | GENERIC_REXGLUE | inferred |
| Timer queue thread | 0006 (`timer_queue.cpp`) | The disruptor library's own blocking strategy passes its predicate and timeout in the wrong order, so it cannot wait with a timeout. The patch carries a correct one and caps the idle wait at 1 s because `time_point::max()` is not handled by every standard library | GENERIC_REXGLUE | inferred; no test |
| Perf counter batching | 0006 (`perf/counter.cpp`) | Not a crash; all guest threads incremented one atomic on every indirect call | GENERIC_REXGLUE | unmeasured here |
| Command processor `WAIT_REG_MEM` | 0003, 0025 | Bounded sleeps instead of the guest's wait value; a signal set after the value was read stays pending, so a wakeup is not lost | GENERIC_REXGLUE | manual |
| `WaitMultiple` contention | 0021 (Linux) | Busy spin while the signaling thread holds the lock | PLATFORM_LINUX | manual / unmeasured |
| Alertable POSIX waits | 0019 | Wakes 1000 times a second while idle; the wait is not alertable now, because that thread has no APCs | PLATFORM_LINUX | manual |
| Occlusion queries | 0011 (D3D12), 0016 (Vulkan) | Ending a submission in the middle of a query lost the query and turned host queries off for the rest of the session; a submission is no longer ended while a query is open, results are written after the submission completes | GENERIC_REXGLUE | manual |
| UI repaint thread | 0029 | New `ui_repaint_thread_` with a mutex and condition variable; shut down and joined in `~Presenter`; the window is touched only while `paint_mode_` is `kUIThreadOnRequest` under `paint_mode_mutex_` | GENERIC_REXGLUE | manual |

Guest fence wake (patch 0009 and `src/gpu_fence_wait.cpp`) is a deliberate race-free handshake, `GAME_ADAPTATION` on the
game side. Safety rules in the waiter, all `CONFIRMED` in the source: the wait is bounded (2 ms); the kernel compares the
word and sleeps in one step (futex / `WaitOnAddress`), so a write between the read and the sleep is never slept through;
a stopped device, an unaligned or unmapped word, a caller other than the expected guest call site, or views of the same
physical page that disagree, fall back to the guest's own polling; and blocking is disabled globally after 8 timeouts
with the word already changed or any failed wait. Validation: manual; no automated test.

Concurrency conventions in the app code (`REUSABLE_INFRASTRUCTURE`): hook code runs on guest threads, so shared state uses
atomics or mutexes (`g_active`, `g_sample_mutex` in `src/window_host.cpp`); UI work goes through `CallInUIThread` or
`CallInUIThreadDeferred`; per-thread hook state is `thread_local` (`t_wait`, `t_live_scope`).

## 4. Lifetimes deliberately not freed
- `g_services` in `src/live_fix.cpp` ("never freed: hooks may still run while the process exits").
- `CursorSync` is created with `new` and retained by the ImGui drawer for the session (`src/window_host.cpp`); the frame
  counter is also drawer-retained (`hcu::CreateFrameCounter`).
- `window_host` is a static listener object; `DetachWindowHost` clears the input-activity callback, restores the cursor
  and clears the active flag, because the guest can end without the window receiving `OnClosing`. Hooks check
  `g_active` before touching `g_window`.
- The title-stop gate in patch 0033 never destroys its mutex / condition variable (`new`'d on first use) because parked
  threads wait on it until the process exits.
Pattern: shared state used by guest-thread hooks is either immortal or guarded by a flag the hook checks first. Classification
`REUSABLE_INFRASTRUCTURE`. Validation: inferred.

## 5. Windows, SDL, Vulkan and library lifetime

### 5.1 Stale or stock runtime next to the game -- `REUSABLE_INFRASTRUCTURE`, `CONFIRMED`
A crash fix in the runtime does nothing if an old `rexruntime` or GPU plugin is loaded. Defences, in introduction order:
`0ec7ded` (Windows stamp, `LINK_DEPENDS` on the SDK DLLs so the program relinks and re-stages them), `dc6feb8` (same
`LINK_DEPENDS` for the Linux shared objects, per-platform stamps), `430fc32` (CMake target `hcu_rexglue_staging` checks the
copies next to the executable on every build; the fingerprint also covers compiler, flags, targets and configurations).
Details in [02-build-system.md](02-build-system.md). The Linux builder additionally refuses a GPU plugin that does not link
the runtime or does not export `rex_gpu_abi_version` and `rex_gpu_create`, and a Release library that names the source or
project folder. Validation: manual (builds fail with the reason).

### 5.2 RenderDoc loaded implicitly (patch 0010, `3020c32`) -- `GENERIC_REXGLUE`, `CONFIRMED`
Detection loaded RenderDoc itself, which activates it. Fix: `DynamicLibrary::LoadExisting`. Test:
`tests/renderdoc_detect_tests.cpp` (Linux). The AppDir check in `tools/package-linux.sh` also rejects any file named like
RenderDoc or a Vulkan layer, JSON file or symbolic link, and any of the three binaries linking RenderDoc.

### 5.3 Vulkan presenter pipeline churn (patch 0035, `3b4a229`)
Every guest frame destroyed and recreated the swapchain paint pipeline after a GPU wait. Not a crash; a stall. See
[01 section 4.5](01-rexglue-core.md#45-cached-swapchain-paint-pipeline-0035----generic_rexglue-platform_linux-confirmed).

### 5.4 SPIR-V exponent bias (patch 0034, `430fc32`)
Wrong power-of-two scaling in the Vulkan fetch path; a correctness fix, not a crash. See
[01 section 4.4](01-rexglue-core.md#44-spir-v-exponent-bias-0034----generic_rexglue-platform_linux-confirmed-bug-visual-effect-likely).

### 5.5 Startup failure when Vulkan is unavailable (`6cf59bd`) -- `PLATFORM_LINUX`, `REUSABLE_INFRASTRUCTURE`
The SDK only logs a presentation failure, so a user without a Vulkan driver saw nothing. `HardCorpsUprisingApp::SetupPresentation`
calls the base; on failure `hcu::RunLauncherWithoutGraphics` opens a software launcher window, and its Play shows `hcu::ReportGraphicsFailure`, an error window drawn in an SDL surface (needs no GPU and no
zenity). SDL's own message box is avoided: it runs zenity first when zenity is installed. Windows: no-op (D3D12 failure is
not handled this way).
Validation: manual (Linux-headless smoke only).

### 5.6 AppImage and libstdc++ / glibc mismatch (`9964f82`, `df29fe2`) -- `PLATFORM_LINUX`
`packaging/linux/AppRun` puts the bundled C++ runtime first only if the system's library lacks `GLIBCXX_3.4.32` (the runtime
needs GCC 13); a newer system copy must win because graphics drivers may need it. Release AppImages are built on Ubuntu 20.04
(glibc 2.31) and `tools/package-linux.sh` stops if any packaged library needs more. Failure mode avoided: loader errors at
start on older distributions. Details: [05-platform-linux.md](05-platform-linux.md), [14-packaging-release.md](14-packaging-release.md).

### 5.7 Windows scheduling and sleep
Patch 0015 (per-thread high-resolution timer) and the above-normal vblank thread priority in 0006 (Windows only) are timing
fixes, not crash fixes. Raising priority on Linux needs privileges and is not done.

## 6. Failure-path hardening in app code (`REUSABLE_INFRASTRUCTURE`, details in other files)
- Installer and DLC: verify-then-swap, previous game folder kept until the swap ([03](03-launcher.md)).
- User data: failed migration keeps the old folder and retries; unwritable roots fall back for the run ([10](10-user-data-settings-migration.md)).
- Offline stats file: write-temp-then-rename, corrupt file moved aside ([11](11-offline-services-profile-achievements.md)).
- Settings: launcher Play does not start the game when the settings save fails ([03](03-launcher.md)).
- Modal dialogs: the Escape listener is registered above the ImGui drawer and detached in `OnShutdown`
  (`src/modal_dialog.cpp`).
- A test-only UB fix: `9bd071d` replaced a `find_if` between iterators of two temporary vectors in `tests/installer_tests.cpp`.
  Lesson: a flaky test can be undefined behaviour in the test.

## 7. Tests that guard crash fixes

| Test (CMake target) | File | Guards | Platforms | In default build |
| --- | --- | --- | --- | --- |
| `hcu_physical_heap_stress` | `tests/physical_heap_stress.cpp` | 0001 | both | no (`EXCLUDE_FROM_ALL`) |
| `hcu_shutdown_order_tests` | `tests/shutdown_order_tests.cpp` | input/window destruction order (`418174b`) | both; Linux needs a display or offscreen | no |
| `hcu_title_thread_shutdown_tests` | `tests/title_thread_shutdown_tests.cpp` | 0033 | both | no |
| `hcu_renderdoc_detect_tests` (+ `hcu_fake_renderdoc`) | `tests/renderdoc_detect_tests.cpp`, `tests/fake_renderdoc.cpp` | 0010 | Linux | no |

The tests cover patches 0001, 0010 and 0033 (three of 37); none of the other 34 patches has an automated test. Run the narrow test for the area you changed during development; run
all of them only in the final release review. Each of the first three uses the real runtime or SDL, so they need the
patched runtime installed and a configured Release project build.

## 8. Crash triage order for a new project (lessons, `REUSABLE_INFRASTRUCTURE`)
1. Know which runtime you run: stamp and staging check first (section 5.1). Half of "the fix doesn't work" is a stale DLL.
2. Classify the phase: startup, steady state, or exit. Exit has two paths (section 0); reproduce through the same one.
3. Read the log for allocator errors (`BaseHeap::Release failed`), unregistered-function messages, and the
   `StopTitleThreads` exit line before opening a debugger.
4. Check object lifetime order at shutdown: input drivers, listeners, ImGui dialogs, then window, then runtime.
5. Check guest threads: who is sleeping in a delay or a wait when the title terminates?
6. Build a deterministic test of the suspected subsystem (stress for races, child-process tests for exit with a hang limit);
   run each case in a child process because the failure may be a crash or a hang.
7. Only then suspect GPU or presenter code; those patches here are performance and correctness-of-image, not crash fixes.
8. Do not claim a fix resolved a user's crash until they confirm; say "fixes a proven defect" instead (issues 6 and 9 are
   still described that way in the release notes).
