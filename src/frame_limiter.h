#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hcu {

inline constexpr int kFrameRateDefault = 60;
inline constexpr int kFrameRateUnlimited = 0;
inline constexpr int kFrameRates[] = {30, 60, 90, 120, 144, 165, 240, kFrameRateUnlimited};

inline bool IsFrameRateChoice(int fps) {
  return std::find(std::begin(kFrameRates), std::end(kFrameRates), fps) != std::end(kFrameRates);
}

// The setting's value, or the default when it holds anything else.
inline int FrameRateOrDefault(int fps) {
  return IsFrameRateChoice(fps) ? fps : kFrameRateDefault;
}

// The stored text as a rate; anything that is not one of the choices, an empty value included, reads as 60.
inline int FrameRateFromText(const std::string& text) {
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  return !text.empty() && end != nullptr && *end == '\0' && value >= 0 && value <= 100000
             ? FrameRateOrDefault(static_cast<int>(value))
             : kFrameRateDefault;
}

inline bool IsHighFrameRate(int fps) {
  return fps == kFrameRateUnlimited || fps > kFrameRateDefault;
}

// 60 is the game's own pacing. Every other rate paces only gameplay: the menus, loading and pause count frames
// and stay at 60.
inline bool PacesGameplayOnly(int fps) {
  return fps != kFrameRateDefault;
}

inline int EffectiveFrameRate(int configured, bool gameplay) {
  return PacesGameplayOnly(configured) && !gameplay ? kFrameRateDefault : configured;
}

// Original: the game flips at its guest vblank, which stays at the SDK's 60 Hz. Immediate: the game's present
// callback flips at once, so only the rates above 60 leave the vblank pacing.
enum class PresentMode { kOriginal, kImmediate };

inline PresentMode PresentModeFor(int fps) {
  return IsHighFrameRate(fps) ? PresentMode::kImmediate : PresentMode::kOriginal;
}

// 60 is held by the original pacing and Unlimited by nothing; the limiter paces the other rates.
inline int ProjectLimiterRate(int fps) {
  return fps == kFrameRateDefault ? kFrameRateUnlimited : fps;
}

inline std::string FrameRateLabel(int fps) {
  if (fps == kFrameRateUnlimited) {
    return "Unlimited";
  }
  return fps == kFrameRateDefault ? "60 Original" : std::to_string(fps);
}

// The launcher's choices: the stored value and its label.
inline std::vector<std::pair<std::string, std::string>> FrameRateChoices() {
  std::vector<std::pair<std::string, std::string>> choices;
  for (const int fps : kFrameRates) {
    choices.emplace_back(std::to_string(fps), FrameRateLabel(fps));
  }
  return choices;
}

using LimiterClock = std::chrono::steady_clock;

inline LimiterClock::duration FramePeriod(int fps) {
  if (fps <= 0) {
    return LimiterClock::duration::zero();
  }
  return std::chrono::duration_cast<LimiterClock::duration>(std::chrono::duration<double>(1.0 / fps));
}

// The schedule of the frame limiter. Next() gives the time the caller has to wait for, if any.
class FrameLimiter {
 public:
  std::optional<LimiterClock::time_point> Next(int fps, LimiterClock::time_point now) {
    const auto period = FramePeriod(fps);
    if (period == LimiterClock::duration::zero()) {
      armed_ = false;
      return std::nullopt;
    }
    if (!armed_ || fps != fps_) {
      armed_ = true;
      fps_ = fps;
      target_ = now + period;
      return std::nullopt;
    }
    const auto deadline = target_;
    target_ = now - deadline > period ? now + period : deadline + period;
    if (deadline <= now) {
      return std::nullopt;
    }
    return deadline;
  }

 private:
  bool armed_ = false;
  int fps_ = 0;
  LimiterClock::time_point target_;
};

// Blocks the calling thread until the deadline: a high-resolution timer for most of it, then a short spin.
void WaitUntil(LimiterClock::time_point deadline);

// The selected frame rate, or the default when the cvar holds something else.
int ConfiguredFrameRate();

// Set by the game thread at the start of each main-loop iteration; read by the present and swap paths.
void SetGameplayPaced(bool gameplay);
int ActiveFrameRate();

// Called by the swap hook before the game's swap: waits for the next frame time.
void PaceGuestFrame();

}  // namespace hcu
