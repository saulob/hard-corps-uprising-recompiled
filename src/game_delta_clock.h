#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace hcu {

// The stage scene advances one fixed step for each whole unit of its accumulator, made for 60 steps per second.
inline constexpr int kSimulationRate = 60;
inline constexpr double kNativeStep = 1.0 / kSimulationRate;

// Three native steps are the 50 ms limit the delta clock allows; a frame shorter than 1 ms is padded up to it.
inline constexpr int kMaxGameSteps = 3;
inline constexpr double kGameDeltaMin = 1.0 / 1000.0;

using SimulationClock = std::chrono::steady_clock;

// The game time one frame is behind, as whole steps of its own when the frame is close to one (its intended speed)
// and as the real interval otherwise, so intermediate rates still track real time. The difference carries over
// within half a step, a frame past the cap loses the excess, and time beyond the cap is dropped rather than owed.
class GameDeltaClock {
 public:
  static constexpr double kSnapTolerance = 0.2;

  double Take(double interval, double step) {
    const double target = interval + owed_;
    const double steps = target / step;
    const double nearest = std::clamp(std::round(steps), 1.0, double(kMaxGameSteps));
    const double cap = kMaxGameSteps * step;
    double delta = std::fabs(steps - nearest) <= kSnapTolerance ? nearest * step : target;
    delta = std::clamp(delta, kGameDeltaMin, cap);
    owed_ = target > cap ? 0.0 : std::clamp(target - delta, -0.5 * step, 0.5 * step);
    return delta;
  }

  void Reset() { owed_ = 0.0; }

  // Real time not yet in the steps given so far; negative when they are ahead of it.
  double owed() const { return owed_; }

 private:
  double owed_ = 0.0;
};

// The scene adds its speed to the accumulator once per frame and ticks once per whole unit, so whole missed steps
// are added before it runs. The fraction stays in the accumulator for the frames that follow.
inline float AccumulatorForSteps(float accumulator, float speed, double steps) {
  return accumulator + static_cast<float>(steps - 1.0) * speed;
}

// One simulation frame of the stage scene: how many native steps it is behind. Resetting, pausing and a new scene
// object re-anchor the clock, so the first frame after any of them never runs a catch-up burst.
class GameplayDeltaClock {
 public:
  double Steps(SimulationClock::time_point now, bool paused, uint32_t scene) {
    if (scene != scene_) {
      Reset();
      scene_ = scene;
    }
    const std::optional<SimulationClock::time_point> previous = previous_;
    previous_ = now;
    if (paused || !previous) {
      clock_.Reset();
      return 0.0;
    }
    const double interval = std::chrono::duration<double>(now - *previous).count();
    return clock_.Take(interval, kNativeStep) / kNativeStep;
  }

  void Reset() {
    previous_.reset();
    clock_.Reset();
  }

 private:
  std::optional<SimulationClock::time_point> previous_;
  uint32_t scene_ = 0;
  GameDeltaClock clock_;
};

}  // namespace hcu
