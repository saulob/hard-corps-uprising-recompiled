#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "frame_limiter.h"
#include "game_delta_clock.h"

namespace hcu {

// An interval longer than this is a stall, not a frame rate.
inline constexpr double kStallInterval = 0.25;

struct StepPlan {
  // Native steps the simulation runs in this iteration: 0 or 1 while frames are faster than a step.
  double steps = 1.0;
  // How far the frame to draw is between the last two simulation states; 1 is the latest.
  double alpha = 1.0;
  bool interpolate = false;
};

// Whole simulation steps of an iteration, for the game functions that run once per step.
inline int WholeSteps(double steps) {
  return steps <= 0.0 ? 0 : std::max(1, static_cast<int>(std::lround(steps)));
}

// kOn: a fixed rate above 60 interpolates from the first frame. kMeasured: Unlimited starts on and follows the rate the
// guest renders at.
enum class InterpolationPolicy { kOff, kOn, kMeasured };

inline InterpolationPolicy InterpolationPolicyFor(int fps) {
  if (!PacesGameplayOnly(fps) || !IsHighFrameRate(fps)) {
    return InterpolationPolicy::kOff;
  }
  return fps == kFrameRateUnlimited ? InterpolationPolicy::kMeasured : InterpolationPolicy::kOn;
}

// Runs the original 60 Hz simulation under a faster presentation.
//
// With interpolation wanted, frames shorter than a step accumulate until one is due, and the position in the next step
// is the interpolation alpha. Otherwise, and for frames of a step or longer, the iteration runs the whole number of
// steps nearest to real time and carries the difference. Steps are always whole, so the stage scene ticks exactly as
// often as the game functions that run once per step.
class SimulationPacer {
 public:
  // The guest render rate is counted over whole windows, and kMeasured changes its decision only after this many
  // windows in a row agree, so hitches and short bursts of frames do not toggle it.
  static constexpr double kRateWindow = 1.0;
  static constexpr int kWindowsToChange = 2;
  static constexpr double kEnableShare = 0.85;
  static constexpr double kDisableShare = 0.95;

  void Reset(InterpolationPolicy policy) {
    previous_.reset();
    remainder_ = 0.0;
    policy_ = policy;
    wanted_ = policy != InterpolationPolicy::kOff;
    window_start_.reset();
    window_frames_ = 0;
    agreeing_windows_ = 0;
    render_hz_ = 0.0;
  }

  StepPlan Advance(SimulationClock::time_point now) {
    if (!previous_ || now < *previous_) {
      previous_ = now;
      remainder_ = 0.0;
      window_start_.reset();
      return {};
    }
    const double interval = std::chrono::duration<double>(now - *previous_).count();
    previous_ = now;
    MeasureRenderRate(now, interval);

    double steps = 0.0;
    if (interval >= kNativeStep || !wanted_) {
      const double target = interval + remainder_;
      const double cap = kMaxGameSteps * kNativeStep;
      steps = std::clamp(std::round(target / kNativeStep), 0.0, double(kMaxGameSteps));
      remainder_ = interval > cap ? 0.0 : target - steps * kNativeStep;
    } else {
      remainder_ += interval;
      if (remainder_ >= kNativeStep) {
        remainder_ -= kNativeStep;
        steps = 1.0;
      }
    }

    StepPlan plan{steps, 1.0, false};
    if (wanted_) {
      plan.interpolate = true;
      plan.alpha = std::clamp(remainder_ / kNativeStep, 0.0, 1.0);
    }
    return plan;
  }

  bool interpolating() const { return wanted_; }
  double render_hz() const { return render_hz_; }
  double remainder() const { return remainder_; }

 private:
  void MeasureRenderRate(SimulationClock::time_point now, double interval) {
    if (interval > kStallInterval || !window_start_) {
      window_start_ = now;
      window_frames_ = 0;
      return;
    }
    ++window_frames_;
    const double elapsed = std::chrono::duration<double>(now - *window_start_).count();
    if (elapsed < kRateWindow) {
      return;
    }
    render_hz_ = window_frames_ / elapsed;
    window_start_ = now;
    window_frames_ = 0;
    if (policy_ != InterpolationPolicy::kMeasured) {
      return;
    }
    const bool toward_change = wanted_ ? render_hz_ < 1.0 / (kNativeStep * kDisableShare)
                                       : render_hz_ >= 1.0 / (kNativeStep * kEnableShare);
    agreeing_windows_ = toward_change ? agreeing_windows_ + 1 : 0;
    if (agreeing_windows_ >= kWindowsToChange) {
      wanted_ = !wanted_;
      agreeing_windows_ = 0;
    }
  }

  std::optional<SimulationClock::time_point> previous_;
  std::optional<SimulationClock::time_point> window_start_;
  double remainder_ = 0.0;
  InterpolationPolicy policy_ = InterpolationPolicy::kOff;
  bool wanted_ = false;
  int window_frames_ = 0;
  int agreeing_windows_ = 0;
  double render_hz_ = 0.0;
};

// What one main-loop iteration showed about the game.
struct GameplaySignals {
  bool stage_ran = false;
  bool stage_ticked = false;
  // The world is paused, the pause menu is open or the task manager is paused.
  bool paused = false;
  // The stage scene object that ran, so a new scene starts over.
  uint32_t scene = 0;
};

// Paced gameplay starts once one stage scene has run and ticked unpaused for kEnterAfter iterations in a row, and
// ends at the first iteration without it, with a pause or with another scene.
class GameplayGate {
 public:
  static constexpr int kEnterAfter = 30;

  bool Next(const GameplaySignals& signals) {
    if (!signals.stage_ran || signals.paused) {
      Reset();
      return false;
    }
    if (signals.scene != scene_) {
      Reset();
      scene_ = signals.scene;
    }
    if (!active_) {
      streak_ = signals.stage_ticked ? streak_ + 1 : 0;
      active_ = streak_ >= kEnterAfter;
    }
    return active_;
  }

  void Reset() {
    active_ = false;
    streak_ = 0;
    scene_ = 0;
  }

  bool active() const { return active_; }

 private:
  bool active_ = false;
  int streak_ = 0;
  uint32_t scene_ = 0;
};

struct IterationPlan {
  bool gameplay = false;
  // Paced gameplay starts at this iteration, or its rate changed: states seen before are not drawn between.
  bool restarted = false;
  bool ended = false;
  StepPlan step;
};

// Decides at the start of each main-loop iteration whether the configured rate paces it, and its steps. Pacing starts
// over whenever gameplay does, so time spent outside it never turns into catch-up steps.
class IterationSchedule {
 public:
  IterationPlan Next(int configured, const GameplaySignals& signals, SimulationClock::time_point now) {
    const bool paced = PacesGameplayOnly(configured);
    if (!paced) {
      gate_.Reset();
    }
    IterationPlan plan;
    plan.gameplay = paced && gate_.Next(signals);
    if (plan.gameplay) {
      if (!active_ || configured != rate_) {
        pacer_.Reset(InterpolationPolicyFor(configured));
        plan.restarted = true;
      }
      plan.step = pacer_.Advance(now);
    } else {
      plan.ended = active_;
    }
    active_ = plan.gameplay;
    rate_ = configured;
    return plan;
  }

  bool active() const { return active_; }
  const SimulationPacer& pacer() const { return pacer_; }

 private:
  GameplayGate gate_;
  SimulationPacer pacer_;
  bool active_ = false;
  int rate_ = kFrameRateDefault;
};

}  // namespace hcu
