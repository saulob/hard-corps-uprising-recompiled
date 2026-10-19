# ReXGlue runtime patches

Hard Corps: Uprising ships ReXGlue **v0.10.0** (tag `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) with
local patches to the runtime library (`rexruntime.dll` / `librexruntime.so`) and to the Xenos GPU plugin
(`rexgpu-xenos.dll` / `librexgpu-xenos.so`, the D3D12 backend on Windows and the Vulkan backend on Linux). Everything
else in the SDK is the stock release.

The platform of a patch is in its file name: `NNNN-name.patch` applies on Windows and Linux, `NNNN-name.windows.patch`
only on Windows and `NNNN-name.linux.patch` only on Linux. The numbers are unique and the patches are applied in name
order. `tools/rexruntime-patch.ps1` and `tools/build-patched-rexruntime.sh` follow the same rule, reject a patch whose
name does not follow it and put only the patches of their platform in the fingerprint, so changing a Linux patch does
not make the Windows runtime out of date, nor the other way round.

The game itself is built against the stock SDK headers. A patch must not move the virtual functions or data members of a
class the game's code or `share/rexglue/rex_app.cpp` uses (`Window`, the public parts of `Presenter`, ...): a new
virtual function goes after all the others, and a new member must not move one the game reads inline.

| Patch | Platform | Library | Change |
| --- | --- | --- | --- |
| [0001-physical-release-order](../patches/rexglue-sdk/0001-physical-release-order.patch) | both | runtime | `PhysicalHeap::Release` in `src/system/xmemory.cpp` |
| [0002-gpu-register-write-bulk-path](../patches/rexglue-sdk/0002-gpu-register-write-bulk-path.patch) | both | GPU plugin | register writes in `src/graphics/command_processor.cpp` |
| [0003-gpu-waits-and-submissions](../patches/rexglue-sdk/0003-gpu-waits-and-submissions.patch) | both | GPU plugin | idle command thread wait, `WAIT_REG_MEM` polling and the `PollBackgroundWork` hook of the common command processor |
| [0004-gpu-draw-path](../patches/rexglue-sdk/0004-gpu-draw-path.patch) | both | GPU plugin | draw extent estimation, primitive cache threshold, shared memory request allocation |
| [0005-render-target-transfer-performance](../patches/rexglue-sdk/0005-render-target-transfer-performance.patch) | both | GPU plugin | draw-overwrite proof of the common render target cache, vertex positions in the draw extent estimator |
| [0006-cpu-timers-and-waits](../patches/rexglue-sdk/0006-cpu-timers-and-waits.patch) | both | runtime | the guest vblank thread, the timer queue thread, guest critical sections and a perf counter (`src/core`, `src/graphics/graphics_system.cpp`, `src/kernel/xboxkrnl/xboxkrnl_rtl.cpp`) |
| [0007-render-target-stencil-regions](../patches/rexglue-sdk/0007-render-target-stencil-regions.patch) | both | GPU plugin | used tile ranges and the first row of a draw, so ownership claims skip the rows above it (render target cache base, draw extent estimator) |
| [0008-edram-dump-dirty-tiles](../patches/rexglue-sdk/0008-edram-dump-dirty-tiles.patch) | both | GPU plugin | per-tile dirty bits of the EDRAM, set by ownership claims and resolve clears (render target cache base) |
| [0009-guest-fence-wake](../patches/rexglue-sdk/0009-guest-fence-wake.patch) | both | GPU plugin | wakes host threads blocked on a guest memory word after the command processor writes it (event write, memory write, conditional write, register to memory); the game's GPU fence wait blocks on the word (`WakeByAddressAll` / futex) |
| [0010-renderdoc-detect-loaded-only](../patches/rexglue-sdk/0010-renderdoc-detect-loaded-only.patch) | both | runtime | `RenderDocAPI::CreateIfConnected` only uses a RenderDoc library the process already loaded (`src/ui/renderdoc_api.cpp`, `DynamicLibrary::LoadExisting` in `src/core/dynlib_*.cpp`) |
| [0011-d3d12-submissions-and-occlusion](../patches/rexglue-sdk/0011-d3d12-submissions-and-occlusion.windows.patch) | Windows | GPU plugin | submission limit, deferred occlusion queries and the resolve preflight in the D3D12 command processor and render target cache |
| [0012-d3d12-render-target-transfers](../patches/rexglue-sdk/0012-d3d12-render-target-transfers.windows.patch) | Windows | GPU plugin | draw-overwrite cutouts and stencil bits of the D3D12 transfers |
| [0013-d3d12-stencil-regions](../patches/rexglue-sdk/0013-d3d12-stencil-regions.windows.patch) | Windows | GPU plugin | per-tile stencil bits, stencil runs and first-row claims in the D3D12 render target cache |
| [0014-d3d12-edram-dump-dirty-tiles](../patches/rexglue-sdk/0014-d3d12-edram-dump-dirty-tiles.windows.patch) | Windows | GPU plugin | dirty-tile dumps in the D3D12 render target cache |
| [0015-windows-high-resolution-sleep](../patches/rexglue-sdk/0015-windows-high-resolution-sleep.windows.patch) | Windows | runtime | high-resolution waitable timer for `Sleep` (`src/core/threading_win.cpp`) |
| [0016-vulkan-submissions-and-occlusion](../patches/rexglue-sdk/0016-vulkan-submissions-and-occlusion.linux.patch) | Linux | GPU plugin | submission limit and deferred occlusion queries in the Vulkan command processor |
| [0017-vulkan-render-target-transfers](../patches/rexglue-sdk/0017-vulkan-render-target-transfers.linux.patch) | Linux | GPU plugin | resolve preflight, draw-overwrite cutouts and per-tile stencil bits in the Vulkan render target cache |
| [0018-vulkan-edram-dump-dirty-tiles](../patches/rexglue-sdk/0018-vulkan-edram-dump-dirty-tiles.linux.patch) | Linux | GPU plugin | dirty-tile dumps in the Vulkan render target cache |
| [0019-linux-command-thread-wait](../patches/rexglue-sdk/0019-linux-command-thread-wait.linux.patch) | Linux | GPU plugin | the idle command thread waits without the alertable flag |
| [0021-posix-wait-multiple-contention](../patches/rexglue-sdk/0021-posix-wait-multiple-contention.linux.patch) | Linux | runtime | `WaitMultiple` in `src/core/threading_posix.cpp` backs off on lock contention instead of spinning |
| [0022-resolve-copy-into-textures](../patches/rexglue-sdk/0022-resolve-copy-into-textures.patch) | both | GPU plugin | which resolves can be copied from the render target into the textures sampling them, the copy regions, and the red/blue order of such textures (render target and texture cache bases, `draw.h`) |
| [0023-d3d12-resolve-copy-into-textures](../patches/rexglue-sdk/0023-d3d12-resolve-copy-into-textures.windows.patch) | Windows | GPU plugin | the copies in the D3D12 render target and texture caches |
| [0024-vulkan-resolve-copy-into-textures](../patches/rexglue-sdk/0024-vulkan-resolve-copy-into-textures.linux.patch) | Linux | both | the copies in the Vulkan render target and texture caches, `vkCmdCopyImage` in the deferred command buffer and the device functions |
| [0025-wait-reg-mem-vblank-wake](../patches/rexglue-sdk/0025-wait-reg-mem-vblank-wake.patch) | both | both | a `WAIT_REG_MEM` past its yields waits for the vblank interrupt instead of a blind 1 ms sleep (common command processor, `graphics_system.cpp`) |
| [0026-vulkan-device-preference](../patches/rexglue-sdk/0026-vulkan-device-preference.patch) | both | runtime | Vulkan auto-selection prefers a discrete GPU among equally capable devices and logs the chosen one |
| [0027-d3d12-adapter-preference](../patches/rexglue-sdk/0027-d3d12-adapter-preference.windows.patch) | Windows | runtime | the D3D12 adapter is the high-performance one unless the player chose power saving for the game in Windows |
| [0028-msaa-2x-host-log](../patches/rexglue-sdk/0028-msaa-2x-host-log.patch) | both | GPU plugin | both render target caches log whether the game's 2x MSAA render targets are host 2x or 4x, and why |
| [0029-ui-repaint-with-guest-frames](../patches/rexglue-sdk/0029-ui-repaint-with-guest-frames.patch) | both | runtime | the presenter draws the UI with the next guest frame instead of presenting the same frame again, and limits UI repaints to the display refresh where nothing else does (`presenter.cpp`, `window_sdl.cpp`) |
| [0030-resolve-deferred-writeback](../patches/rexglue-sdk/0030-resolve-deferred-writeback.patch) | both | GPU plugin | which resolves copied into a texture may leave their memory unwritten, and when it is written after all (render target and texture cache bases) |
| [0031-d3d12-resolve-deferred-writeback](../patches/rexglue-sdk/0031-d3d12-resolve-deferred-writeback.windows.patch) | Windows | GPU plugin | the deferral and the later writes in the D3D12 render target and texture caches |
| [0032-vulkan-resolve-deferred-writeback](../patches/rexglue-sdk/0032-vulkan-resolve-deferred-writeback.linux.patch) | Linux | GPU plugin | the deferral and the later writes in the Vulkan render target and texture caches |
| [0033-stop-title-threads](../patches/rexglue-sdk/0033-stop-title-threads.patch) | both | runtime | title termination and `Runtime::Shutdown` stop every guest thread of the title before anything is torn down (`kernel_state.cpp`, `runtime.cpp`, the kernel waits in `xobject.cpp` and `xthread.cpp`) |
| [0034-spirv-texture-exp-adjust](../patches/rexglue-sdk/0034-spirv-texture-exp-adjust.linux.patch) | Linux | GPU plugin | the SPIR-V texture fetch takes the exponent bias from fetch constant word 3, as the DXBC one does (`spirv_translator_fetch.cpp`) |
| [0035-cache-vulkan-swapchain-paint-pipeline](../patches/rexglue-sdk/0035-cache-vulkan-swapchain-paint-pipeline.linux.patch) | Linux | runtime | the Vulkan presenter records the format its swapchain paint pipeline was created for, so it is reused instead of recreated after a GPU wait on every frame (`vulkan_presenter.cpp`, [ReXGlue issue #507](https://github.com/rexglue/rexglue-sdk/issues/507)) |
| [0036-internal-render-scale](../patches/rexglue-sdk/0036-internal-render-scale.patch) | both | GPU plugin | the `hcu_internal_render_scale` cvar and the split of the render resolution into the host render target scale (quarters of a guest pixel) and the whole scale of the EDRAM, resolves and textures; sizes, viewport and scissor of the host render targets; the pixel position of the shader translators (render target cache base, `draw.cpp`, texture cache base, DXBC and SPIR-V translators) |
| [0037-d3d12-internal-render-scale](../patches/rexglue-sdk/0037-d3d12-internal-render-scale.windows.patch) | Windows | GPU plugin | host render target sizes, transfers, clears and viewport at the host scale, and the EDRAM dump taking its samples from the smaller render targets in the D3D12 render target cache and command processor |
| [0038-vulkan-internal-render-scale](../patches/rexglue-sdk/0038-vulkan-internal-render-scale.linux.patch) | Linux | GPU plugin | the same for the Vulkan render target cache and command processor |
| [0040-d3d12-transfer-short-circuit](../patches/rexglue-sdk/0040-d3d12-transfer-short-circuit.windows.patch) | Windows | GPU plugin | a draw with no render target transfers and no resolve clear skips the transfer pass of the D3D12 render target cache |
| [0041-d3d12-sampler-memo](../patches/rexglue-sdk/0041-d3d12-sampler-memo.windows.patch) | Windows | GPU plugin | the D3D12 command processor recomputes sampler parameters only when the shader, the fetch constants or `anisotropic_override` changed |

Pins: [patches/rexglue-sdk/baseline.env](../patches/rexglue-sdk/baseline.env) holds the tag, the commit and the stock
file hashes.

## What each platform gets

The optimizations are in the common code where the cost is in the common code, and in the backend where it is in the
backend. A "not needed" is a statement about the platform, not about the API it uses.

| Optimization | Windows (D3D12) | Linux (Vulkan) |
| --- | --- | --- |
| Physical release order (0001) | common | common |
| Register write bulk path, known-register table (0002) | common | common |
| Idle command thread wait, `WAIT_REG_MEM` polling (0003) | common | common, plus 0019 (the wait is not alertable, so it is not polled in 1 ms slices) |
| Draw extent on the CPU, page state, primitive cache, request ranges (0004) | common | common |
| Guest vblank wait, timer queue thread, critical section spin, perf counter (0006) | common | common |
| Draw-overwrite proof, rows above a draw, dirty bits (0005, 0007, 0008) | common | common |
| Submission limit and deferred occlusion queries | 0011 | 0016 |
| Resolve preflight removed | 0011 | 0017 |
| Draw-overwrite cutouts in transfers | 0012 | 0017 |
| Per-tile stencil bits and runs | 0013 (always needed without stencil reference output) | 0017 (needed without `VK_EXT_shader_stencil_export`) |
| Dirty-tile EDRAM dumps | 0014 | 0018 |
| GPU fence wake and wait (0009 and the game) | `WakeByAddressAll` / `WaitOnAddress` | futex wake / wait |
| High-resolution sleep | 0015 | not needed: `Sleep` is `nanosleep`, precise already |
| Vblank thread priority (in 0006) | above normal | not applicable: raising it needs `CAP_SYS_NICE` or an rlimit |
| RenderDoc detection (0010) | common | common |
| Wait on several handles | not needed: one kernel wait | 0021 (contention back-off) |
| Resolved render target copied into the textures sampling it (0022) | 0023 (`CopyTextureRegion`) | 0024 (`vkCmdCopyImage`) |
| `WAIT_REG_MEM` woken by the vblank interrupt (0025) | common | common |
| GPU preference | 0027 (DXGI high performance) | 0026 (discrete Vulkan device) |
| Host samples of 2x MSAA logged (0028) | common | common |
| UI drawn with the next guest frame (0029) | common; vertical blank ticks without guest frames | common; display refresh limit without guest frames |
| Resolve memory written only when needed (0030) | 0031 | 0032 |
| Texture fetch exponent bias from word 3 | already correct in the DXBC translator | 0034 |
| Host render targets smaller than the draw resolution (0036) | 0037 | 0038 |
| Swapchain paint pipeline kept while the format is unchanged | not applicable: the D3D12 presenter has no such cache | 0035 |

The POSIX wait on several handles (`WaitMultiple` in `threading_posix.cpp`) polls with 1 ms sleeps; Windows waits in the
kernel. 0021 only removes its spin on lock contention; the polling stays, since a notification scheme across the
condition variables of the handles is a redesign of the runtime's synchronization.

## Why 0010

`RenderDocAPI::CreateIfConnected` is meant to find a RenderDoc that is already attached to the process, and is called
when the Vulkan instance is created and by the GPU debug marker check. It looked for it with `DynamicLibrary::Load`,
which is `dlopen("librenderdoc.so")` on Linux and `LoadLibraryW("renderdoc.dll")` on Windows. Those also load the
library when the loader can find it, and loading RenderDoc's library in-process is what activates it: the reporter
of [issue #6](https://github.com/saulob/hard-corps-uprising-recompiled/issues/6) on SteamOS got `RenderDoc API
initialized` in the log and RenderDoc's overlay without having attached anything.

- **Lookup.** `DynamicLibrary::LoadExisting` takes a reference to a library the process has already loaded and never
  loads one: `dlopen` with `RTLD_NOLOAD` on POSIX (null if the library is not loaded), `GetModuleHandleExW` without
  `GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT` on Windows (it adds a reference). Both leave the object owning one
  reference, which the existing `Close` (`dlclose`/`FreeLibrary`) releases. `Load` is unchanged.
- **Caller.** `CreateIfConnected` uses `LoadExisting`, so it returns `nullptr` when RenderDoc is not in the process,
  and still returns the API when RenderDoc was injected or preloaded. macOS keeps returning `nullptr` before any
  lookup. This does not change what the GPU does, and nothing here is evidence about CPU or GPU load.
- **Test.** `tests/renderdoc_detect_tests.cpp` (CMake target `hcu_renderdoc_detect_tests`, Linux, not part of the
  default build) checks that `CreateIfConnected` returns `nullptr` and loads nothing when no RenderDoc is around, and
  when a fake `librenderdoc.so` is on the loader path but not loaded; that it finds the API when the library was
  loaded on purpose; and that the library unloads once its last owner is gone.

## Why 0009

The game's main thread spent its time in a loop polling the GPU fence word of the device. The command processor writes
that word with an event write packet on its own thread, so a waiter can sleep on the word's address instead.

- **Wake.** After each guest memory write by the command processor (event write, memory write, conditional write,
  register to memory) `WakeGuestWordWaiters` wakes the waiters of the written address: `WakeByAddressAll` on Windows,
  `futex(FUTEX_WAKE_PRIVATE)` on Linux (the plugin and the game are one process, so the private futex is enough; the
  wake is a system call each time, a few per frame). Nothing else changes in the packet handlers. A full fence precedes
  the wake so the store is ordered before the wake looks for waiters.
- **Wait.** `src/gpu_fence_wait.cpp` in the game hooks the begin and poll helpers of that one loop and waits on the same
  physical-view address with the value it last saw, bounded by 2 ms: `WaitOnAddress` on Windows,
  `futex(FUTEX_WAIT_PRIVATE)` on Linux. A write after the value was read makes the wait return at once (the kernel
  compares the word and sleeps in one step; `EAGAIN`), so no write is slept through. A signal (`EINTR`) ends the wait
  like a wake, and the guest re-checks its condition. The offset between the guest view and the host view of the 0xE0
  heap is `rex::memory::detail::PhysicalHostOffset`, zero on Linux.
- **Safety.** Any other caller, a stopped device, an unaligned or unmapped word, or a word whose two views differ keeps
  the guest's own polling. After 8 waits that ended by timeout with the word already changed, or after a wait that
  failed (`EFAULT` on a page the host cannot read, for one), blocking is turned off.

## Why 0008 and 0014

0008 sets the dirty bits in the common render target cache; 0014 (D3D12) and 0018 (Vulkan) shrink the dumps with them.

A resolve first dumps the host render targets owning its source tiles into the EDRAM buffer, then copies from there.
At 4x the dump was repeated for every resolve over the whole source rectangle, even for tiles no draw touched since
the last dump.

- **Dirty tiles.** One bit per EDRAM tile says the owning host render target may differ from what the EDRAM buffer
  last got from it. It is set when ownership of a tile changes (any claim, including the transfer and the host depth
  store into it), for the tiles of a resolve clear, and for the tiles a draw can write: the claimed range of each
  color target with a nonzero write mask, and of the depth target when depth or stencil can be written. Dumping a
  tile clears it. Everything starts dirty, and the render target cache reset sets everything dirty.
- **Dump.** Each dump dispatch is shrunk to the smallest rectangle containing its dirty tiles, and dropped if there
  are none. The guest-visible resolve copy is unchanged: it still reads the same EDRAM tiles, which are current.
- **Not done.** Skipping the resolve copy itself (the destination would need a proof against CPU writes, other
  resolves and the scaled resolve memory), a lazy dump, and a copy-free resolve (no way to prove every reader of the
  EDRAM buffer).

## Why 0007 and 0013

0007 is the common part (used tile ranges and the first row of a draw); 0013 is the per-tile stencil bits of the D3D12
render target cache.

The heavy 4x scenes spent most ownership transfer draws on stencil bits: one depth render target mask that only ever
gained bits, so a single color to depth transfer made every depth to depth transfer after it draw eight bit passes.

- **Stencil bits per tile.** A depth render target keeps the bits that may be set per EDRAM tile. Draws mark only the
  tiles they claim and only with the bits their stencil state can write (a failing or never-writing stencil state
  marks nothing); clears assign the cleared value. A transfer takes the bits of its source tiles, so the transferred
  range is split into runs of equal bits, drawn once per bit that run has, and the destination stencil is cleared only
  where it may hold something. Where a transfer replaces the destination it assigns the source bits instead of
  adding them.
- **Stencil through color.** A depth to color transfer packs the stencil into the low byte of 8_8_8_8, 2_10_10_10 and
  16_16 targets, so such a color target keeps the bits of the tiles it received. Any draw or clear that writes color
  there makes them unknown again, and a transfer back to depth then uses all bits.
- **Rows above a draw.** The draw extent estimator also returns a conservative first row (scissor, viewport, or the
  vertex positions when clipping is disabled). The render target cache claims ownership only from that row down, in
  whole tile rows, so the tiles above are neither transferred nor claimed. The claim stays a single range, so
  columns are not narrowed.

## Why 0006

The common parts apply on Linux too (the profiling counter, the vblank wait, the timer queue thread and the guest
critical sections); the sleep timer is `0015`, Windows only: Linux `Sleep` is `nanosleep`, which already lasts as
long as asked, and raising the priority of the vblank thread needs privileges that a normal Linux desktop does not
give, so it stays at the default there.

- **Profiling counter.** Builds other than Release (RelWithDebInfo, the default of `tools\build.ps1`) compile the
  recompiled game with `REXGLUE_ENABLE_PERF_COUNTERS`, so every guest indirect call (every virtual call) ran
  `rex::perf::IncrementCounter(kFunctionsDispatched)`: a call into the runtime and a locked add on one shared counter.
  With several guest threads running at once, which is what loading is, they all fought for that one cache line.
  The counter is now added to a per-thread count that is published in batches of 1024. Release has no such call.
- **Sleeps.** `Sleep` and the alertable sleep lasted up to a timer tick longer than asked (a 1 ms sleep took 1.8 ms
  on the test machine). From 1 ms up they now wait on a high-resolution waitable timer, as King Kong Recompiled's
  frame limiter does. Waits with a timeout (`WaitForSingleObject` and friends) still follow the system timer.
- **Guest vblank thread.** It polled with `Sleep(1)` (about 1.8 ms), so a vblank could come up to two milliseconds
  after its time; it now sleeps until the next vblank is due. It runs one priority level above normal, since it sleeps
  almost all the time and a late vblank delays the guest's frame.
- **Timer queue thread.** The thread behind `HighResolutionTimer` (the 1 ms `KeTimeStampBundle` updater is the only
  user on Windows) waited with disruptorplus' spin wait strategy. Each wait began with about 4000 `pause`
  instructions and 19 `SwitchToThread` calls before a 1 ms sleep, so the thread woke about 500 times a second and
  spun for a good part of each wake-up for as long as the game ran. It now blocks on a condition variable that
  `QueueTimer` signals. disruptorplus' own blocking strategy passes its predicate and timeout to
  `std::condition_variable` in the wrong order and cannot wait with a timeout, so the patch carries a small one.
  Callbacks run up to a timer tick late.
- **Guest critical sections.** A thread that finds a guest critical section taken now polls the lock for up to 256
  pauses before it registers as a waiter and sleeps in the kernel. Only a free lock without waiters is ever taken, so
  a thread that already waits is not overtaken.

## Why 0003

- **Waiting for commands.** The idle command thread spun through 500 `SwitchToThread` calls before sleeping on the
  write pointer event; it now yields 32 times and sleeps (The Simpsons Game Recomp). `WAIT_REG_MEM` treats the guest's
  wait value as a poll interval, re-polls 16 times before sleeping and sleeps 1 ms at most (the Simpsons fork caps
  it at 500 us), instead of sleeping the whole value, so the ring does not stay blocked after the condition is met.
- **Background work.** Both waits call the new virtual `PollBackgroundWork` of the command processor, which returns
  whether it has to be polled again soon (the idle wait then lasts 1 ms instead of 5 ms). The backends use it to write
  deferred occlusion query results and to end the open submission when there is nothing more to record (0011, 0016).

## Why 0011

The Direct3D 12 half of 0003; 0016 is the Vulkan one.

- **Occlusion queries.** Ending a guest query ended the D3D12 submission and waited for the GPU to finish all work
  queued before it, on every query. The result is now written to the guest once the submission that holds the
  `ResolveQueryData` has completed, and the guest sees the query as unfinished until then (its `0xFFFFFEED` markers
  stay in place), as with a GPU running behind the command processor. The command processor polls for completed queries
  while it waits for commands or for a `WAIT_REG_MEM` condition, and ends the submission holding them when it has
  nothing more to record. Ending a submission in the middle of a query no longer happens, because it lost the query and
  turned host queries off for the rest of the session.
- **Submissions.** Submitting at the end of every primary buffer (`d3d12_submit_on_primary_buffer_end`) now only
  happens while fewer than three submissions are in flight, which is when it keeps the GPU fed. With more queued, the
  commands stay in the open submission until the swap, or until the GPU queue gets short again.
- **Resolves.** The `direct_host_resolve` preflight only computed the dump rectangles that `DumpRenderTargets` then
  computed again before doing the same dump, so it is skipped in the D3D12 backend.

## Why 0005 and 0012

0005 is the part in the common render target cache (the draw-overwrite proof and the vertex positions of the draw
extent estimator); 0012 uses it in the D3D12 transfers and adds the stencil bit mask there.

Ownership transfers (copies of the old contents of an EDRAM range when another host render target takes it over) were
the main GPU cost at high resolution scales: their count did not grow with the scale, their pixels did.

- **Stencil bits.** Without pixel shader stencil reference output (NVIDIA), a depth transfer copies the stencil one
  bit at a time: a stencil clear over the rectangle, then up to eight full-rectangle draws. Each depth render target
  now tracks which stencil bits may be set anywhere in it. It is created zeroed, and gains bits from draws whose
  stencil operations can write (the bits of the write mask, or of the reference for replace), from the stencil values
  of resolve clears, and from transfers (all bits when the source is a color render target). A transfer draws only the
  bits that may be set in its source, and clears the destination stencil only if the destination may hold any. A game
  that never writes the stencil no longer pays for the eight draws.
- **Draws that replace their target.** A rectangle list draw (what a Direct3D 9 clear or a full-screen pass is) that
  covers a rectangle with the depth and stencil tests always passing, no blending, all components written and no
  pixel kill or depth output replaces the old contents there, so they are not transferred (the transfer is cut around
  the rectangle, as for the clear of a resolve). The vertex shader runs on the CPU for its three vertices, so any
  shader the interpreter handles counts, not only the XDK clear shader. The rectangle is shrunk by a pixel on every
  side, so it is covered at every resolution scale and sample count. A draw that replaces only the depth keeps the
  stencil transfers. Adapted from The Simpsons Game Recomp's Vulkan render target cache, which does it for its clears.
- **Clears and transfers** otherwise as before: the clear rectangle of a resolve was already cut out of the transfers.

## Why 0004

- **Draw extent.** `execute_unclipped_draw_vs_on_cpu` is on, as in Xenia (the SDK release has it off). A draw with
  clipping disabled and an 8192 scissor, which is what a Direct3D 9 clear is, was taken to cover the whole EDRAM, so
  it moved ownership of render target ranges that had nothing to do with it, and moved it back afterwards. The
  vertex shader of such a draw now runs on the CPU to find the rows it really covers. The Simpsons Game Recomp turned
  it on for the same reason and measured about 2 ms per frame of such transfers on the Steam Deck.
- **Frame-end page state.** `clear_memory_page_state` is off (the SDK release has it on). With it on, every frame end
  forgets which guest pages the GPU copy is valid for, so the next frame uploads every vertex and index buffer it uses
  again, and each upload batch is a barrier of the shared memory buffer between draws. Write watches already
  invalidate the pages the guest changes, as in Xenia, which has no such option. The Simpsons Game Recomp turned it
  off and measured about 4 ms of GPU time per frame at 2x on the Steam Deck. If a buffer or texture ever shows stale
  data, `clear_memory_page_state = true` in `settings.toml` brings the old behavior back.
- **Primitive processor.** `primitive_processor_cache_min_indices` is 4096, Xenia's default (the SDK release has 0):
  converted index buffers under that size cost more in the cache lookup and the memory watch than the conversion.
  The cache's invalidation test also compared a cached entry's end with the end of the written range instead of its
  start, so a write that covered a whole entry, the guest rewriting an index buffer in place, did not invalidate it
  (the same fix is in The Simpsons Game Recomp's SDK fork and matches Xenia).
- **Shared memory.** `SharedMemory::RequestRanges` reuses a member vector instead of allocating one on every call,
  including the single-range `RequestRange` made for every draw's indices.

## Why 0002

The GPU command thread looked every register write up in `RegisterFile::GetRegisterInfo`, a switch over the whole
register table, only to decide whether to log an unknown register; the patch checks a table built once. Runs of
render-state registers (`0x2000`-`0x3FFF`) are copied as a block, as the constant registers already were, and a type-0
packet of three or more registers that stays within one block-copyable span (render state, float constants, fetch
constants, bool/loop constants) is applied as one range instead of one virtual `WriteRegister` call per register
(measured: the block path costs about the same as three single writes and stays flat beyond that). The registers with
side effects (scratch, coherency status, gamma ramp) are all below `0x2000`, and shorter packets keep the ordered
path. Both backends take the block path through the base class. The same two changes are in The Simpsons Game Recomp's
SDK fork.

## Why 0001

The guest's physical memory (`0xA0000000`, `0xC0000000` and `0xE0000000`) is tracked twice: in the shared parent
heap of physical pages and in the page table of each of those views. `PhysicalHeap::Release` freed the parent pages
first and cleared the view's entries last, and the callbacks in between give up the global lock. A second thread could
allocate the just-freed pages and pin them in the view over the stale entries, and the first release then erased the
new allocation's entries. The new owner's own release later failed with
`BaseHeap::Release failed because address is not a region start`, and the view's page table no longer matched the
memory in use. The patch releases the view first and the parent pages last, so the pages cannot be handed out again
before the view has dropped them. The callbacks still run first, while the region is intact, and `out_region_size`
still reports the parent's size. This is the same fix as MindOver-Recomp's `0008-physical-release-order.patch`.

Reported in [issue #6](https://github.com/saulob/hard-corps-uprising-recompiled/issues/6) on SteamOS, where the logs
showed these errors around exiting the game. The code is the same on Windows; the error needs two threads to hit the
window between the two releases, so how often it happens depends on timing.

## Why 0016

The Vulkan command processor had the two problems that 0003 and 0011 fix in D3D12.

- **Occlusion queries.** Ending a guest query ended the submission and waited for the GPU to finish everything queued
  (`AwaitAllQueueOperationsCompletion`), on every query. The query is now copied into the readback buffer inside the
  submission, and its result is written to the guest once that submission has completed (`CompleteOcclusionQueries`,
  run whenever the submission fences are checked), so the guest sees the query as unfinished until then, as with a GPU
  running behind the command processor. A result still pending for the same address is dropped when a new query for it
  ends, the query indices are only reused after the pending results have been written, and a TRANSFER to HOST barrier
  makes the copied result readable. A submission is not ended while a query is open (it would end the query, and a
  query cannot span command buffers, which turned host queries off for the session).
- **Submissions.** `vulkan_submit_on_primary_buffer_end` only submits at the end of a primary buffer while fewer than
  three submissions are in flight. The base class polls `PollBackgroundWork` while the command processor idles or sits
  in `WAIT_REG_MEM`; the Vulkan override ends the open submission when it holds pending query results or when the early
  submission rule allows it, and checks the fences without blocking.

## Why 0017

Vulkan's default render target path is host render targets, so it has the same ownership transfers as D3D12, through
the common render target cache. This patch gives the Vulkan transfer code the parts of 0005 and 0007 that live in the
backend, on top of the common parts that Vulkan gets from 0005 to 0008.

- **Resolve preflight.** `Resolve` no longer runs `TryResolveCopyDirectly`, which computed the dump rectangles and then
  called `DumpRenderTargets`, which computes them again.
- **Draws that replace their target.** `Update` asks the common `GetDrawOverwrittenRenderTargets` whether the draw
  replaces what it writes (same proof as in D3D12: rectangle list, tests always passing, no blending, whole mask, no
  kill or depth output, vertices run on the CPU) and cuts that rectangle out of the transfers. A draw that only
  replaces the depth keeps the stencil transfers, and is only considered without `VK_EXT_shader_stencil_export`, since
  with it the transfer shader writes the stencil together with the depth.
- **Stencil bits.** Without `VK_EXT_shader_stencil_export` (NVIDIA, for one) a depth transfer copies the stencil one
  bit at a time. Each render target keeps the bits that may be set per EDRAM tile; transfers split their range into
  runs of equal bits, draw once per bit of each run, and clear the destination stencil only where it may hold
  something. Draws add the bits their stencil state can write, resolve clears assign the cleared value, and transfers
  assign the source's bits where they replace the destination. Vulkan images are not created zeroed, so every tile
  starts with all bits possible: the first transfer into a tile clears its stencil as before. With the extension (AMD,
  Intel, the Steam Deck) the stencil bit draws do not exist, so the tracking has no effect on what is drawn.

## Why 0018

The Vulkan counterpart of 0008 and 0014. The common render target cache sets the dirty bits of the EDRAM tiles; this
patch sets them for the tiles a draw can write (color targets with a nonzero mask, depth when depth or stencil can be
written), shrinks each dump dispatch of `DumpRenderTargets` to the dirty tiles of its rectangle, drops the invocations
without any, and clears the bits of a tile after its dispatch has been recorded. A dump that cannot be recorded (no
pipeline) leaves its tiles dirty. The EDRAM buffer is not transitioned when nothing needs dumping.

## Why 0019

On POSIX an alertable wait (`rex::thread::Wait` with the alertable flag) is not one blocking wait: it is polled in 1 ms
slices so queued APCs can run in between. The idle command thread waits on the write pointer event with that flag, so
its 5 ms wait woke five times (1000 times a second while idle) although the thread has no APCs. The wait is no longer
alertable. The same patch is not needed on Windows, where an alertable wait is one kernel wait. Other alertable waits,
the guest's own, keep the slices: delivering an APC to a blocked POSIX waiter needs a wake-up from the thread's
user-callback queue, which is a change to the runtime's synchronization that this work does not make.

## Why 0021

`WaitMultiple` try-locks the mutex of every handle before checking them. When one is held (the thread signaling it,
such as the SDL audio callback releasing the audio worker's client semaphores), it yielded and retried at once, so
the waiter spun for as long as the signaling side was busy; it also allocated its lock vector and read the clock on
every pass, even for waits without a timeout. Now it sleeps 100 microseconds before retrying, keeps one vector for
the whole wait and reads the clock only when there is a timeout. Which handle satisfies the wait, the 1 ms polling
and the timeouts are unchanged. Ported from The Simpsons Game recompilation (`030a9412`), where this loop serving
the audio worker took 42% of the process's CPU time on Linux.

## Why 0022, 0023 and 0024

A resolve writes the render target into memory, and a texture sampling that memory is loaded from it
again: a compute decode into a scratch buffer and a copy into the texture, at the resolution scale. When
the resolve copies a whole single-sampled render target from its origin, without conversion, into the
same `k_8_8_8_8` or `k_2_10_10_10` bits the texture samples, the texture can be copied from the render
target instead, and the load is skipped. The resolve still writes the memory.

The copy is made only when it gives the same texels as the load:
- one render target owns every resolved tile and starts at the resolve origin, with the same pitch and
  a format of the same family; the destination is the texture's base address, page aligned, and the
  texture is a single-level tiled 2D texture with the resolve's pitch, format and endianness, inside
  the resolved rectangle, up to date before the resolve and not a copy destination still in flight;
- the resolve shader's half-pixel offset fill (`resolve_resolution_scale_fill_half_pixel_offset`, the
  left and top `scale / 2` host pixels take the first fully covered column and row) is reproduced with
  extra copies;
- a resolve swapping red and blue leaves the texture with them swapped, and every view of it (the
  bindings, the front buffer) swaps them back; a normal load clears that.

Anything else (MSAA, depth, other formats, exponent bias, offsets, render targets aliasing the area,
mipmapped textures) is resolved and loaded as before. `resolve_copy_into_textures` turns the copies off.
On Vulkan the eligible render targets get the transfer source usage.

## Why 0025

The game's present makes the GPU wait (`WAIT_REG_MEM`, equal) for a word that its vblank interrupt
handler clears when the flip is done. Past its yields, the command processor polled that word in 1 ms
sleeps, so it resumed up to 1 ms after the vblank. The vblank thread now signals an event after running
the interrupt, and the wait waits on it for at most the same 1 ms. A signal set after the value was read
stays pending, so it isn't lost; other waits keep the 1 ms poll.

## Why 0026 and 0027

With several GPUs, D3D12 took the first adapter DXGI enumerated and Vulkan the first of the equally
capable devices, which is the integrated GPU on many laptops. D3D12 now enumerates by high-performance
preference, or by minimum power when the player chose power saving for the game in Windows' graphics
settings; `d3d12_adapter` still picks one by index. Vulkan prefers a discrete GPU among devices with the
same features, unless `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT` or
`__NV_PRIME_RENDER_OFFLOAD` already chose one; `vulkan_device` still picks one by index. The game also
exports `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`.

## Why 0028

`native_2x_msaa = false` doesn't turn multisampling off: the game's 2x MSAA render targets get host 4x
MSAA. Both backends now log which host sample count those render targets get and why.

## Why 0029

While any ImGui dialog exists (the game keeps its frame counter and cursor timer as dialogs), the presenter
paints from the UI thread, and the ImGui drawer asks for a repaint after every frame. Each guest frame was
presented as it arrived, and the UI repaint presented it again at the next vertical blank: about 120
presents a second for 60 guest frames. On Linux, with no vertical blank ticks, the repaint ran as fast as
the GPU allowed. A repaint for the UI alone now waits for the next guest frame, which is presented with the
UI, for up to 50 ms. Without guest frames (the launcher, a stall) it waits for a vertical blank tick on
Windows as before, or for the display refresh interval elsewhere. Resizes, surface changes, OS repaints and
changes to the dialogs still paint at once.

## Why 0030, 0031 and 0032

With resolution scaling, a resolve copied into the one texture sampling exactly its rectangle (0022) still
dumped the render target to the EDRAM buffer and wrote the scaled resolve memory, which nothing else read
while the resolve kept repeating. `resolve_defer_writeback` now skips both and keeps the texture, which
then can't be evicted, as the only copy. The memory is written before a texture load reads any of it,
before another resolve partly overwrites it, and when the resolve isn't repeated in the next frame: the
texture goes through the render target's own dump shader into the EDRAM buffer (those tiles are then dirty
again) and the saved resolve runs as it would have. That gives the same bits, as the texture holds what
the render target had there, and the resolve's half-pixel fill takes the pixels it already took. A later
resolve writing exactly the same bytes drops the unwritten one. Without resolution scaling, other GPU work
reads the resolved memory directly, and resolve readback needs it at once, so neither defers.

## Why 0033

Exit Game ends in `XamLoaderLaunchTitle`, which runs `KernelState::TerminateTitle` on the main guest thread. It woke
the other guest threads, gave them 200 ms to reach a kernel wait and exit, then cleared its flag and dropped the rest
from its thread list. The game's polling worker sleeps 500 ms per round in `KeDelayExecutionThread`, so it often
outlived that window and kept running while `Runtime::Shutdown` destroyed the kernel state and unmapped guest memory:
a SIGSEGV on Linux a few seconds after "Execution complete" under CPU load.

- **Stop.** A guest thread of the title counts as running from its start until it exits, except while it is blocked in
  a kernel wait (`XObject::Wait`, `WaitMultiple`, `SignalAndWait`, `XThread::Delay`). `XThread::StopTitleThreads`
  waits until none runs and then closes the gate: a thread leaving its wait after that never returns to guest code and
  stays parked until the process exits. Nothing is killed; runtime threads (vblank, audio, kernel dispatch) are not
  title threads and are stopped by their subsystems as before.
- **Lifetime.** `TerminateTitle` keeps its flag set and keeps the object of each thread that did not exit, since that
  host thread still uses it. `Runtime::Shutdown` stops the title's threads (terminating the title if nothing did) and
  waits for the thread that terminated it to exit before it tears anything down. A thread still running guest code
  after 2 s can be neither stopped nor killed safely, so the process then ends at once without any teardown, as on
  window close.
- **Test.** `tests/title_thread_shutdown_tests.cpp` (CMake target `hcu_title_thread_shutdown_tests`, not part of the
  default build) runs the runtime with a worker asleep in a 500 ms delay, one blocked on a cancelled timer and one
  running guest code, terminates the title from the main guest thread and tears the runtime down: no guest code may
  run after the main thread has finished. A last case adds a thread that never reaches a kernel wait, for which the
  runtime must end the process instead.

## Why 0034

The texture fetch's result exponent bias, `exp_adjust`, is a signed 6-bit field at bit 13 of fetch constant word 3. The
SPIR-V translator read bits 13:18 of word 4, which belong to the LOD bias, so the Vulkan backend scaled a fetch by a
wrong power of two wherever the two differ, which can darken an image or lose a glow. The DXBC translator (D3D12)
already reads word 3, and Windows does not build the SPIR-V translator, so the patch is Linux only.

## Why 0036, 0037 and 0038

The launcher's Internal render resolution draws the host render targets at 100%, 75% or 50% of the Render resolution
(`resolution_scale`, `hcu_internal_render_scale`). ReXGlue's draw resolution scale is a whole number everywhere: the
EDRAM buffer holds `scale * scale` host samples for each guest sample, a resolve writes `scale * scale` texels for each
guest texel into the scaled resolve memory, which is laid out in guest pages times `scale * scale`, the texture cache
and the resolve and texture load shaders (bytecode only, built without their sources) take the scale in bit fields, and
a scale of 1 means no scaling at all. 1x at 75% and 50% would even be smaller than the guest picture. So the scale is
not made fractional; it is split in two instead.

- **Host scale.** The host render targets, the viewport, the scissor, the clears, the transfers between render targets
  and the pixel position seen by pixel shaders are `host_scale_quarters / 4` of the guest picture
  (`RenderTargetCache::host_scale_quarters_x()`, `GuestToHostX`). The 80x16 sample EDRAM tile and the 8 pixel
  resolve granularity stay whole numbers of host pixels at every quarter.
- **Draw resolution scale.** The EDRAM buffer, the resolves and the textures keep a whole scale, the smallest one that
  holds the host render targets: `ceil(host_scale)`. At 100%, and wherever the host scale is a whole number, the
  two are equal and nothing differs from before: 4x at 75% is a 3x draw scale, 4x at 50% 2x and 2x at 50% 1x.
  `TextureCache::GetConfigInternalRenderScale` makes the split.
- **Where they meet.** The dump of the host render targets into the EDRAM buffer, which every resolve starts with,
  takes the value of the host pixel that contains the center of each EDRAM sample, per axis, from the in-tile pixel and
  sample index (also for 2x and 4x MSAA and for depth, stencil and 64bpp). That is the only place the two scales
  meet: the transfers between render targets sample render targets of the same scale.

| Internal render resolution | Render resolution | Host render targets | Draw resolution scale |
| --- | --- | --- | --- |
| 100% | 1x, 2x, 3x, 4x | 1280x720, 2560x1440, 3840x2160, 5120x2880 | 1x, 2x, 3x, 4x |
| 75% | 1x, 2x, 3x, 4x | 960x540, 1920x1080, 2880x1620, 3840x2160 | 1x, 2x, 3x, 3x |
| 50% | 1x, 2x, 3x, 4x | 640x360, 1280x720, 1920x1080, 2560x1440 | 1x, 1x, 2x, 2x |

What does not follow the host scale, on purpose:

- **Resolve copy into textures and deferred writeback (0022 to 0024, 0030 to 0032)** copy the render target into a
  texture of the draw resolution scale, so they only run while the two scales are equal.
- **Host depth (`IsHostDepthEncodingDifferent`)** is stored in the EDRAM buffer at the draw resolution scale and is
  not used while the host render targets are smaller; depth is converted on every transfer then.
- **Memory export** only picks one host pixel for each guest pixel at whole host scales above 1.
- **Rasterizer-ordered views (D3D12 on Intel by default or `render_target_path_d3d12 = rov`) and fragment shader
  interlock (Vulkan `render_target_path_vulkan = fsi`)** draw into the EDRAM buffer itself, at the draw resolution
  scale. They draw at that scale instead and log a warning. The game avoids this on D3D12: below 100% it sets
  `render_target_path_d3d12 = rtv` (`ApplyInternalRenderPath`, when the game starts) unless the player set a path.

The pixel position (`ps_param_gen`) is divided by the host scale, the resolve fill, texture offsets and scaled texture
coordinates keep the draw resolution scale. The viewport is rounded outward to whole host pixels and the vertex
transform follows the rounded edges, the scissor is rounded outward as well.

## Why 0040 and 0041

The D3D12 command processor did work on every draw that most draws did not need: `PerformTransfersAndResolveClears`
walked the render targets, descriptors and barriers even when there was nothing to transfer or clear, and
`UpdateBindings` recomputed the sampler parameters of every sampler of every draw. Both are per-draw CPU cost that
grows with the draw count, which is what the integrated GPU case showed.

- **Transfer short-circuit (0040).** A draw whose render targets have no pending transfers and which is not a resolve
  clear records nothing in the transfer pass, so it returns at the top of the function. Clears pass a clear rectangle
  and resolve clear values, and are never skipped; a bound render target with a transfer keeps the full path, so
  ownership transitions, host depth storing, stencil bits, dirty tiles and the deferred writeback are unchanged.
- **Sampler memoization (0041).** `GetSamplerParameters` reads only the shader's sampler bindings, the fetch
  constants of the binding and `anisotropic_override`, so the parameters are recomputed only when the shader changes,
  a fetch constant is written (a generation counter) or the override changes. The memo stores one key per stage and is
  invalidated at frame open with the other constant buffer bindings. `hcu_d3d12_sampler_memo = false` restores the
  old behavior.

Both are Windows-only and gated by a cvar (`hcu_d3d12_transfer_short_circuit`, `hcu_d3d12_sampler_memo`, both on by
default), and are checked by `tests/d3d12_transfer_short_circuit_tests.cpp` and
`tests/d3d12_sampler_memo_tests.cpp`. They are adapted from Crackdown ReXGlue patch 0082, without its dependency on
the common draw state memo (0080) or the asynchronous submission (0081).

## Windows-only patches

- 0011, 0012, 0013 and 0014 are the D3D12 halves of 0003, 0005, 0007 and 0008 (submissions and occlusion queries,
  transfers, stencil bits and dump dirty tiles in `src/graphics/d3d12`); the common halves also apply on Linux.
- 0015 is the high-resolution waitable timer of `threading_win.cpp` (see 0006).
- 0023 and 0031 are the D3D12 halves of 0022 and 0030 (0024 and 0032 are the Vulkan ones), and 0027 the D3D12
  counterpart of 0026.
- 0037 is the D3D12 half of 0036 (0038 is the Vulkan one).

## Building the patched runtime

On Windows (Visual Studio with the LLVM/Clang component, and git), after `tools\codegen.ps1` has put the SDK in
`tools\rexglue`, nothing else is needed:

```powershell
.\tools\build.ps1            # or .\tools\build.ps1 -Run
```

Before configuring, `tools\build.ps1` checks the installed runtime and GPU plugin against the current baseline and
patches (see below). If they do not match it runs `tools\build-patched-rexruntime.ps1` itself, checks again, and stops
with the reasons if they still do not match; if they match, ReXGlue is not rebuilt. Adding, removing, renaming,
editing or reordering a patch, or changing `baseline.env` or the build settings in `tools\rexruntime-patch.ps1`,
therefore makes the next `build.ps1` rebuild ReXGlue (the first time it fetches the v0.10.0 source, which needs
network access, and a build takes a few minutes). The Debug configuration uses the stock `rexruntimed.dll` and
`rexgpu-xenosd.dll` and skips the check.

`tools\build-patched-rexruntime.ps1` stays available as a manual command:

```powershell
.\tools\build-patched-rexruntime.ps1                 # fetch v0.10.0 (network), patch, build, install
.\tools\build-patched-rexruntime.ps1 -Restore        # put the stock runtime and GPU plugin back
```

It applies the patches that apply to Windows (the shared and the `.windows.patch` ones, in name order), builds the RelWithDebInfo and Release runtime and GPU
plugin into `out\rexglue-sdk-src` (use `-WorkDir` for a shorter path) and replaces them in `tools\rexglue\win-amd64`.
The Debug files stay stock. After `-Restore`, the next `build.ps1` patches them again.

Linux (Ubuntu 20.04, see [linux.md](linux.md)): `tools/package-linux.sh` runs `tools/build-patched-rexruntime.sh`
itself the first time and whenever the check described below fails. The script applies the patches that apply to
Linux (the shared and the `.linux.patch` ones, in name order; `--list` prints them with the fingerprint) and builds
`librexruntime.so` and `librexgpu-xenos.so`, the Vulkan GPU plugin, which is a separate library on Linux as well. The
RelWithDebInfo (`librexruntimerd.so`, `librexgpu-xenosrd.so`) and Release libraries are patched, with the stock RUNPATH
(`$ORIGIN:$ORIGIN/../lib`) instead of the build folder CMake would put there; the Debug libraries stay stock. Besides
the packages in linux.md it needs git, network access and the SDK's build dependencies, which
`tools/setup-linux-build-env.sh` installs on Ubuntu 20.04 (it builds libdecor and PipeWire from pinned sources, since
20.04 does not package them).

Both scripts check the source is exactly the pinned commit, stop if a patch no longer applies, compile with
`-ffile-prefix-map` so no build folder ends up in the library, and refuse a result that equals the stock file.

## What the build verifies

The scripts record the installed files in `rexruntime-patch.txt` next to the SDK: the source commit, the fingerprint
of everything the files are built from (the SDK version, tag and commit, the platform, the clang and MSVC toolset
versions, the compiler flags, targets and configurations in `tools\rexruntime-patch.ps1`, and every patch's name and
hash in name order), and the SHA-256 of
each installed file, the runtime and the GPU plugin included. The check (`Test-PatchedRuntime`) fails if the
fingerprint differs from the current one, if any recorded file does not match, or if one of them is the stock release
file; it names what changed, for example `patches added: ...`. Timestamps are not used.

`tools\build.ps1` runs that check before the build and updates the runtime when it fails. After the build it copies
`rexruntime*.dll` and `rexgpu-xenos*.dll` of the configuration (`d`, `rd` or no suffix) from `tools\rexglue\win-amd64\bin`
next to the executable when they differ, then fails unless the executable exists, both DLLs next to it are
byte-identical to the installed ones and the stamp matches. The same check runs once more before `-Run` starts the
game, and a failure prints the expected and the actual file.

Builds that do not go through `build.ps1` (a preset, an IDE) check the same: the `hcu_rexglue_check` target runs
`build-patched-rexruntime.ps1 -Check` (Linux: `build-patched-rexruntime.sh --check`) before the program is built, and
`hcu_rexglue_staging` copies the configuration's runtime and GPU plugin next to it and checks them with `-Staged`
(`--staged`). In Debug only the staged copies are checked. A failure names the remedy, `tools\build.ps1` or
`tools/build.sh`.

`tools/package-release.ps1` builds through `build.ps1`, then accepts the runtime and the GPU plugin only if the check
passes; the packaged DLLs must also be identical to the installed ones. With `-SkipBuild` it does not update the
runtime and fails when it is out of date.

On Linux the stamp (`rexruntime-patch.txt`) has the same fields (`inputs_sha256` over the SDK version, tag and commit,
the platform, the clang version, the compiler flags, the RUNPATH, the targets, the configurations and every Linux
patch's name and hash; `patch_sha256`;
`patches`) and the SHA-256 of the four libraries. `tools/build-patched-rexruntime.sh --check` fails if the
fingerprint differs, if a library does not match the stamp or if one is the stock file, and names what changed
(for example `patches added: ...`). `tools/build.sh` and `tools/package-linux.sh` run that check and rebuild when it
fails; `package-linux.sh` then checks with `--staged` that the libraries next to the built program and in the AppDir
are byte-identical to the installed ones. The build also refuses a GPU plugin that does not link the runtime or
export `rex_gpu_abi_version` and `rex_gpu_create`, and a Release library that names the source, SDK or project
folder. The project build relinks the program when `lib/librexruntime*.so` or `lib/librexgpu-xenos*.so` in
`tools/rexglue/linux-amd64` change (`LINK_DEPENDS` in `CMakeLists.txt`), so the post-build copies cannot leave a stale
library beside it. The AppDir check also fails on
any file named like RenderDoc or a Vulkan layer (in any case, including `implicit_layer.d` and `explicit_layer.d`), any
JSON file or symbolic link, and any of the three binaries linking RenderDoc, besides the exact file list it already
enforces.

## Allocator stress test

`tests/physical_heap_stress.cpp` (CMake target `hcu_physical_heap_stress`, not part of the default build) has 8 threads
allocate and release physical memory at once on the `0xA0000000` and `0xE0000000` views, and passes only if no release
fails. With the stock runtime it fails; with the patched one it passes.

```powershell
.\tools\build.ps1 -Config Release -BuildDir build-release
.\tools\build-patched-rexruntime.ps1 -StressTest -SkipBuild    # test the installed runtime
```

On Linux: `tools/build-patched-rexruntime.sh --stress <sdk dir> <build dir>`, with a configured Release project
build in `<build dir>`. Both run the test beside the installed runtime. To compare, run `-Restore` first and repeat
the test with the stock runtime.

## RenderDoc detection test

On Linux, with a configured Release project build in `<build dir>` and the patched runtime installed:

```bash
tools/build-patched-rexruntime.sh --loader-test <sdk dir> <build dir>
```

It runs the test beside the installed runtime, which must print `PASS`, and then beside the stock runtime from the SDK
download kept next to `<sdk dir>`, which must fail by loading the fake RenderDoc; that shows the test detects the bug.
