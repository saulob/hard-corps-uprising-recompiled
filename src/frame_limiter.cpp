#include "frame_limiter.h"

#include <atomic>
#include <mutex>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "settings.h"

namespace hcu {
namespace {

// The present callback's interval, in bits 8-11 of its argument.
constexpr uint32_t kPresentIntervalMask = 0xF00;

FrameLimiter g_limiter;
std::mutex g_limiter_mutex;
std::atomic<bool> g_gameplay_paced{false};

}  // namespace

int ConfiguredFrameRate() {
  return FrameRateOrDefault(REXCVAR_GET(hcu_frame_rate));
}

void SetGameplayPaced(bool gameplay) {
  g_gameplay_paced.store(gameplay, std::memory_order_relaxed);
}

int ActiveFrameRate() {
  return EffectiveFrameRate(ConfiguredFrameRate(), g_gameplay_paced.load(std::memory_order_relaxed));
}

// A change of rate re-arms the limiter, so time spent at another rate is never waited for or caught up.
void PaceGuestFrame() {
  std::optional<LimiterClock::time_point> deadline;
  {
    std::lock_guard lock(g_limiter_mutex);
    deadline = g_limiter.Next(ProjectLimiterRate(ActiveFrameRate()), LimiterClock::now());
  }
  if (deadline) {
    WaitUntil(*deadline);
  }
}

}  // namespace hcu

// The D3D present callback: zero in the interval bits flips at once instead of at the next guest vblank, so rates
// above 60 do not need a faster vblank.
REX_EXTERN(__imp__sub_82551A70);
REX_HOOK_RAW(sub_82551A70) {
  if (hcu::PresentModeFor(hcu::ActiveFrameRate()) == hcu::PresentMode::kImmediate) {
    ctx.r3.u32 &= ~hcu::kPresentIntervalMask;
  }
  __imp__sub_82551A70(ctx, base);
}
