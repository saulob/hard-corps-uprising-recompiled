// High-refresh timing tests: the 60 Hz simulation under 30 and 90 to Unlimited presentation, the stage scene's
// step accumulator it drives, jitter, stalls, bursts, rate changes and scene resets. Pure logic plus source checks;
// the hooks need the running game.
// Run: hcu_simulation_pacer_tests

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "simulation_pacer.h"

using namespace hcu;
using std::chrono::nanoseconds;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

constexpr double kStep = kNativeStep;
constexpr double kEps = 1e-9;

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

SimulationClock::time_point At(double seconds) {
  return SimulationClock::time_point{} + nanoseconds(static_cast<long long>(seconds * 1e9));
}

// The stage scene as the game runs it: its speed is added to the accumulator on every call, and it ticks while the
// accumulator is above zero, one unit each.
struct Stage {
  float accumulator = 0.0f;
  float speed = 1.0f;

  int Run(double steps, bool paused = false) {
    if (paused) {
      accumulator = 0.0f;
      return 0;
    }
    accumulator = AccumulatorForSteps(accumulator, speed, steps);
    accumulator += speed;
    int ran = 0;
    float left = accumulator;
    while (left > 0.0f) {
      ++ran;
      left -= 1.0f;
    }
    if (ran > 0) {
      accumulator = left;
    }
    return ran;
  }
};

struct Totals {
  double seconds = 0.0;
  long long iterations = 0;
  long long ticks = 0;
  // Runs of the game functions that count steps; they must match the scene's ticks.
  long long counted = 0;
  long long idle = 0;
  int largest = 0;
  long long interpolated = 0;
  double alpha_min = 1.0;
  double alpha_max = 0.0;
  bool alpha_in_range = true;
};

// Runs the schedule over frames whose spacing comes from `next`, with the stage past its settling period.
struct Rig {
  IterationSchedule schedule;
  Stage stage;
  GameplaySignals signals{true, true, false, 1};
  double now = 1.0;
  Totals totals;

  IterationPlan Iterate(int configured, double interval) {
    now += interval;
    const IterationPlan plan = schedule.Next(configured, signals, At(now));
    const int ran = stage.Run(plan.gameplay ? plan.step.steps : 1.0);
    totals.ticks += ran;
    totals.counted += plan.gameplay ? WholeSteps(plan.step.steps) : 1;
    totals.idle += ran == 0;
    totals.largest = std::max(totals.largest, ran);
    if (plan.gameplay) {
      totals.interpolated += plan.step.interpolate;
      totals.alpha_min = std::min(totals.alpha_min, plan.step.alpha);
      totals.alpha_max = std::max(totals.alpha_max, plan.step.alpha);
      totals.alpha_in_range &= plan.step.alpha >= 0.0 && plan.step.alpha <= 1.0;
    }
    totals.seconds += interval;
    ++totals.iterations;
    return plan;
  }

  // Past the settling period, so the schedule is paced.
  void Settle(int configured, double interval) {
    for (int i = 0; i < GameplayGate::kEnterAfter + 2; ++i) {
      Iterate(configured, interval);
    }
    totals = {};
  }

  void Run(int configured, double seconds, const std::function<double()>& next) {
    const double end = totals.seconds + seconds;
    while (totals.seconds < end) {
      Iterate(configured, next());
    }
  }
};

double TicksPerSecond(const Totals& t) { return double(t.ticks) / t.seconds; }

// Frame intervals of a clock that ticks every `period`, each tick up to `jitter` early or late.
class AnchoredTicks {
 public:
  AnchoredTicks(double period, double jitter) : period_(period), jitter_(jitter) {}
  double operator()() {
    state_ = state_ * 1664525u + 1013904223u;
    const double offset = (double(state_ >> 8) / double(1u << 24) * 2.0 - 1.0) * jitter_;
    const double interval = period_ + offset - previous_offset_;
    previous_offset_ = offset;
    return interval;
  }

 private:
  double period_;
  double jitter_;
  double previous_offset_ = 0.0;
  uint32_t state_ = 12345;
};

class Cycle {
 public:
  explicit Cycle(std::vector<double> intervals) : intervals_(std::move(intervals)) {}
  double operator()() { return intervals_[next_++ % intervals_.size()]; }

 private:
  std::vector<double> intervals_;
  std::size_t next_ = 0;
};

void TestStageModel() {
  std::printf("stage scene accumulator model\n");
  Stage stage;
  CHECK(stage.Run(1.0) == 1);
  CHECK(stage.accumulator == 0.0f);
  CHECK(stage.Run(0.0) == 0);
  CHECK(stage.accumulator == 0.0f);
  CHECK(stage.Run(2.0) == 2);
  CHECK(stage.Run(3.0) == 3);
  CHECK(stage.accumulator == 0.0f);
  Stage fractional;
  CHECK(fractional.Run(0.5) == 1);
  CHECK(fractional.accumulator == -0.5f);
  CHECK(fractional.Run(0.5) == 0);
  CHECK(fractional.Run(1.0) == 1);
}

void TestPresentationRates() {
  std::printf("simulation stays at 60 Hz under every rate\n");
  for (const int rate : {90, 120, 144, 165, 240}) {
    Rig rig;
    rig.Settle(rate, 1.0 / rate);
    rig.Run(rate, 20.0, [&] { return 1.0 / rate; });
    CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.1);
    CHECK(rig.totals.largest == 1);
    CHECK(rig.totals.interpolated == rig.totals.iterations);
    CHECK(rig.totals.alpha_in_range);
    CHECK(rig.totals.idle > 0);
  }
  // Unlimited has no target; whatever the machine renders, the game runs at 60.
  for (const double fps : {70.0, 100.0, 300.0, 1000.0}) {
    Rig rig;
    rig.Settle(kFrameRateUnlimited, 1.0 / fps);
    rig.Run(kFrameRateUnlimited, 20.0, [&] { return 1.0 / fps; });
    CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.1);
    CHECK(rig.totals.largest == 1);
  }
}

void TestThirtyFps() {
  std::printf("30 FPS keeps real-time speed with two steps a frame\n");
  Rig rig;
  rig.Settle(30, 1.0 / 30.0);
  rig.Run(30, 20.0, [] { return 1.0 / 30.0; });
  CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.1);
  CHECK(rig.totals.largest == 2);
  CHECK(rig.totals.idle == 0);
  CHECK(rig.totals.interpolated == 0);
  CHECK(rig.totals.alpha_min == 1.0);

  Rig jittered;
  jittered.Settle(30, 1.0 / 30.0);
  jittered.Run(30, 20.0, AnchoredTicks(1.0 / 30.0, 0.004));
  CHECK(std::fabs(TicksPerSecond(jittered.totals) - 60.0) < 0.2);
  CHECK(jittered.totals.largest <= kMaxGameSteps);
}

void TestCountedFunctionsMatchScene() {
  std::printf("the step-counting game functions run as often as the scene ticks\n");
  for (const double fps : {20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 55.0, 59.0, 61.0, 65.0, 75.0}) {
    Rig rig;
    rig.Settle(144, 1.0 / fps);
    rig.Run(144, 30.0, [&] { return 1.0 / fps; });
    CHECK(rig.totals.counted == rig.totals.ticks);
    CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.3);
    CHECK(rig.totals.largest <= kMaxGameSteps);
  }
  Rig jittered;
  jittered.Settle(144, 1.0 / 40.0);
  jittered.Run(144, 30.0, AnchoredTicks(1.0 / 40.0, 0.006));
  CHECK(jittered.totals.counted == jittered.totals.ticks);
  Rig hitch;
  hitch.Settle(144, 1.0 / 144.0);
  hitch.Run(144, 20.0, Cycle({1.0 / 144.0, 1.0 / 144.0, 1.0 / 144.0, 0.1, 1.0 / 144.0, 1.0 / 144.0}));
  CHECK(hitch.totals.counted == hitch.totals.ticks);
}

void TestHitchKeepsInterpolation() {
  std::printf("one hitch does not turn interpolation off\n");
  Rig rig;
  rig.Settle(144, 1.0 / 144.0);
  rig.Run(144, 3.0, [] { return 1.0 / 144.0; });
  rig.Iterate(144, 0.1);
  for (int i = 0; i < 20; ++i) {
    CHECK(rig.Iterate(144, 1.0 / 144.0).step.interpolate);
  }
}

void TestOriginalRateIsUntouched() {
  std::printf("60 never starts the paced schedule\n");
  Rig rig;
  for (int i = 0; i < 600; ++i) {
    const IterationPlan plan = rig.Iterate(60, kStep);
    CHECK(!plan.gameplay);
    CHECK(!plan.restarted);
    CHECK(!plan.ended);
  }
  CHECK(!rig.schedule.active());
}

void TestNearSixty() {
  std::printf("exact 60, slightly below and slightly above\n");
  // A measured presentation that cannot beat 60 behaves like the original: about one step a frame, real-time speed,
  // no interpolation once the measurement has settled.
  for (const double fps : {60.0, 59.5, 58.0, 61.0, 62.0}) {
    Rig rig;
    rig.Settle(kFrameRateUnlimited, 1.0 / fps);
    rig.Run(kFrameRateUnlimited, 5.0, [&] { return 1.0 / fps; });
    rig.totals = {};
    rig.Run(kFrameRateUnlimited, 30.0, [&] { return 1.0 / fps; });
    CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.3);
    CHECK(rig.totals.largest <= 2);
    CHECK(rig.totals.alpha_in_range);
    CHECK(rig.totals.interpolated == 0);
    CHECK(std::fabs(double(rig.totals.ticks) * kStep - rig.totals.seconds) < 2.0 * kStep + 1e-6);
  }
  // A cadence that stays within the snap tolerance gives exactly one step every frame.
  Rig steady;
  steady.Settle(kFrameRateUnlimited, kStep);
  steady.Run(kFrameRateUnlimited, 5.0, [] { return kStep; });
  for (int i = 0; i < 3000; ++i) {
    const IterationPlan plan = steady.Iterate(kFrameRateUnlimited, i % 2 == 0 ? kStep * 1.06 : kStep * 0.94);
    CHECK(plan.step.steps == 1.0);
    CHECK(!plan.step.interpolate);
    CHECK(plan.step.alpha == 1.0);
  }
}

void TestEarlyAndLateFrames() {
  std::printf("alternating early and late frames\n");
  for (const int rate : {90, 120, 144}) {
    Rig rig;
    const double period = 1.0 / rate;
    rig.Settle(rate, period);
    rig.Run(rate, 20.0, Cycle({period * 0.6, period * 1.4}));
    CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.2);
    CHECK(rig.totals.largest <= 1);
    CHECK(rig.totals.alpha_in_range);
  }
  Rig around_sixty;
  around_sixty.Settle(144, kStep);
  around_sixty.Run(144, 20.0, Cycle({kStep * 0.8, kStep * 1.2}));
  CHECK(std::fabs(double(around_sixty.totals.ticks) * kStep - around_sixty.totals.seconds) < 2.0 * kStep);
  CHECK(around_sixty.totals.largest <= 2);
}

void TestStallsAndBursts() {
  std::printf("host stall and burst after it\n");
  Rig rig;
  rig.Settle(144, 1.0 / 144.0);
  rig.Run(144, 5.0, [] { return 1.0 / 144.0; });
  // A 400 ms stall advances the cap and drops the rest.
  const IterationPlan stall = rig.Iterate(144, 0.4);
  CHECK(stall.gameplay);
  CHECK(rig.totals.largest <= kMaxGameSteps);
  CHECK(rig.stage.accumulator <= 0.0f);
  // The frames right after it are a burst of short ones: still 60 steps a second, never a second catch-up.
  rig.totals = {};
  rig.Run(144, 10.0, [&] { return 1.0 / 144.0; });
  CHECK(std::fabs(TicksPerSecond(rig.totals) - 60.0) < 0.2);
  CHECK(rig.totals.largest == 1);

  for (const std::vector<double>& pattern :
       std::vector<std::vector<double>>{{0.044, 0.002, 0.002, 0.002}, {0.008, 0.036}, {0.032, 0.003, 0.003, 0.003, 0.032}}) {
    Rig burst;
    burst.Settle(144, 1.0 / 144.0);
    burst.Run(144, 20.0, Cycle(pattern));
    CHECK(burst.totals.largest <= kMaxGameSteps);
    CHECK(std::fabs(double(burst.totals.ticks) * kStep - burst.totals.seconds) < 6.0 * kStep);
  }

  // Repeated stalls never owe time or run past the cap.
  Rig repeated;
  repeated.Settle(120, 1.0 / 120.0);
  for (int i = 0; i < 50; ++i) {
    for (int j = 0; j < 120; ++j) {
      repeated.Iterate(120, 1.0 / 120.0);
    }
    repeated.Iterate(120, 0.5);
    CHECK(repeated.stage.accumulator <= 0.0f);
  }
  CHECK(repeated.totals.largest <= kMaxGameSteps);
}

void TestRateTransitions() {
  std::printf("rate changes\n");
  Rig up;
  for (int i = 0; i < 600; ++i) {
    up.Iterate(60, kStep);
  }
  CHECK(up.totals.ticks == 600);
  // 60 to 120: the paced schedule starts after the settling period, with no catch-up burst.
  up.totals = {};
  while (!up.Iterate(120, kStep).gameplay) {
    CHECK(up.totals.iterations < 100);
  }
  CHECK(up.totals.ticks == up.totals.iterations);
  up.totals = {};
  for (int i = 0; i < 1200; ++i) {
    up.Iterate(120, 1.0 / 120.0);
  }
  CHECK(up.totals.largest <= 1);
  CHECK(std::fabs(TicksPerSecond(up.totals) - 60.0) < 0.5);
  // 120 to 60: the original cadence returns at once.
  up.totals = {};
  for (int i = 0; i < 600; ++i) {
    const IterationPlan plan = up.Iterate(60, kStep);
    CHECK(!plan.gameplay);
  }
  CHECK(up.totals.ticks == 600);

  // A change from 120 to 144 inside one stretch of gameplay starts the pacing over.
  Rig change;
  change.Settle(120, 1.0 / 120.0);
  const IterationPlan plan = change.Iterate(144, 1.0 / 144.0);
  CHECK(plan.restarted);
  CHECK(plan.step.steps == 1.0);
  change.totals = {};
  change.Run(144, 10.0, [] { return 1.0 / 144.0; });
  CHECK(std::fabs(TicksPerSecond(change.totals) - 60.0) < 0.2);

  // Frame time dropping from 144 to 50 FPS and back: interpolation follows the real rate.
  Rig drop;
  drop.Settle(kFrameRateUnlimited, 1.0 / 144.0);
  drop.Run(kFrameRateUnlimited, 5.0, [] { return 1.0 / 144.0; });
  CHECK(drop.schedule.pacer().interpolating());
  drop.Run(kFrameRateUnlimited, 5.0, [] { return 1.0 / 50.0; });
  CHECK(!drop.schedule.pacer().interpolating());
  drop.Run(kFrameRateUnlimited, 5.0, [] { return 1.0 / 144.0; });
  CHECK(drop.schedule.pacer().interpolating());
}

bool ActiveAfter(int configured, double render_hz, double seconds = 10.0) {
  Rig rig;
  rig.Settle(configured, 1.0 / render_hz);
  rig.Run(configured, seconds, [&] { return 1.0 / render_hz; });
  return rig.schedule.pacer().interpolating();
}

void TestPolicyPerRate() {
  std::printf("interpolation policy per rate\n");
  CHECK(InterpolationPolicyFor(30) == InterpolationPolicy::kOff);
  CHECK(InterpolationPolicyFor(60) == InterpolationPolicy::kOff);
  for (const int fps : {90, 120, 144, 165, 240}) {
    CHECK(InterpolationPolicyFor(fps) == InterpolationPolicy::kOn);
  }
  CHECK(InterpolationPolicyFor(kFrameRateUnlimited) == InterpolationPolicy::kMeasured);

  Rig thirty;
  thirty.Settle(30, 1.0 / 30.0);
  thirty.Run(30, 5.0, [] { return 1.0 / 30.0; });
  CHECK(!thirty.schedule.pacer().interpolating());
  CHECK(thirty.totals.interpolated == 0);

  // A fixed rate interpolates from the first paced frame and keeps doing so at whatever rate the guest renders.
  for (const int fps : {90, 120, 144, 165, 240}) {
    Rig rig;
    rig.Settle(fps, 1.0 / fps);
    CHECK(rig.schedule.pacer().interpolating());
    rig.Run(fps, 1.0, [&] { return 1.0 / fps; });
    CHECK(rig.totals.interpolated == rig.totals.iterations);
    CHECK(ActiveAfter(fps, 60.0, 20.0));
    CHECK(ActiveAfter(fps, 30.0, 20.0));
    CHECK(ActiveAfter(fps, double(fps), 20.0));
  }
  CHECK(!ActiveAfter(60, 60.0));
}

void TestMeasuredUnlimited() {
  std::printf("unlimited follows the guest render rate\n");
  Rig start;
  start.Settle(kFrameRateUnlimited, kStep);
  CHECK(start.schedule.pacer().interpolating());

  CHECK(ActiveAfter(kFrameRateUnlimited, 144.0));
  CHECK(ActiveAfter(kFrameRateUnlimited, 300.0));
  CHECK(!ActiveAfter(kFrameRateUnlimited, 60.0));
  CHECK(!ActiveAfter(kFrameRateUnlimited, 63.0));
  CHECK(ActiveAfter(kFrameRateUnlimited, 72.0));

  // Off needs two windows in a row below the disable rate, so the first alone keeps it on.
  Rig window;
  window.Settle(kFrameRateUnlimited, 1.0 / 144.0);
  window.Run(kFrameRateUnlimited, 5.0, [] { return 1.0 / 144.0; });
  window.Run(kFrameRateUnlimited, 1.5, [] { return kStep; });
  CHECK(window.schedule.pacer().interpolating());
  window.Run(kFrameRateUnlimited, 2.0, [] { return kStep; });
  CHECK(!window.schedule.pacer().interpolating());

  Rig swing;
  swing.Settle(kFrameRateUnlimited, 1.0 / 144.0);
  swing.Run(kFrameRateUnlimited, 5.0, [] { return 1.0 / 144.0; });
  CHECK(swing.schedule.pacer().interpolating());
  swing.Run(kFrameRateUnlimited, 5.0, [] { return kStep; });
  CHECK(!swing.schedule.pacer().interpolating());
  swing.Run(kFrameRateUnlimited, 5.0, [] { return 1.0 / 144.0; });
  CHECK(swing.schedule.pacer().interpolating());

  // Short drops and hitches do not turn it off.
  Rig dips;
  dips.Settle(kFrameRateUnlimited, 1.0 / 144.0);
  dips.Run(kFrameRateUnlimited, 3.0, [] { return 1.0 / 144.0; });
  for (int i = 0; i < 20; ++i) {
    dips.Run(kFrameRateUnlimited, 0.6, [] { return kStep; });
    dips.Run(kFrameRateUnlimited, 1.5, [] { return 1.0 / 144.0; });
    CHECK(dips.schedule.pacer().interpolating());
  }
  Rig hitches;
  hitches.Settle(kFrameRateUnlimited, 1.0 / 144.0);
  for (int i = 0; i < 20; ++i) {
    hitches.Run(kFrameRateUnlimited, 1.0, [] { return 1.0 / 144.0; });
    hitches.Iterate(kFrameRateUnlimited, 0.4);
    CHECK(hitches.schedule.pacer().interpolating());
  }

  // Jitter around the thresholds never flips it every frame.
  for (const double hz : {61.0, 63.0, 66.0, 68.0, 70.0, 72.0}) {
    Rig rig;
    rig.Settle(kFrameRateUnlimited, 1.0 / hz);
    int toggles = 0;
    bool previous = rig.schedule.pacer().interpolating();
    for (int i = 0; i < 6000; ++i) {
      rig.Iterate(kFrameRateUnlimited, 1.0 / hz + (i % 2 ? 0.002 : -0.002));
      toggles += rig.schedule.pacer().interpolating() != previous;
      previous = rig.schedule.pacer().interpolating();
    }
    CHECK(toggles <= 2);
  }
}

void TestPausedAndReset() {
  std::printf("pause and scene reset leave no interpolation\n");
  Rig paused;
  paused.Settle(144, 1.0 / 144.0);
  paused.signals.paused = true;
  for (int i = 0; i < 10; ++i) {
    const IterationPlan plan = paused.Iterate(144, 1.0 / 144.0);
    CHECK(!plan.gameplay && !plan.step.interpolate);
  }
  paused.signals.paused = false;
  int settled = 0;
  while (!paused.Iterate(144, 1.0 / 144.0).gameplay) {
    CHECK(++settled < 100);
  }
  CHECK(paused.schedule.pacer().interpolating());

  Rig unlimited;
  unlimited.Settle(kFrameRateUnlimited, kStep);
  unlimited.Run(kFrameRateUnlimited, 6.0, [] { return kStep; });
  CHECK(!unlimited.schedule.pacer().interpolating());
  unlimited.signals.scene = 9;
  CHECK(!unlimited.Iterate(kFrameRateUnlimited, kStep).gameplay);
  while (!unlimited.Iterate(kFrameRateUnlimited, kStep).gameplay) {
  }
  CHECK(unlimited.schedule.pacer().interpolating());
}

void TestAlphaIsSteady() {
  std::printf("interpolation alpha advances evenly\n");
  const double period = 1.0 / 144.0;
  Rig rig;
  rig.Settle(144, period);
  double previous_position = -1.0;
  long long tick = 0;
  double worst = 0.0;
  for (int i = 0; i < 4000; ++i) {
    const IterationPlan plan = rig.Iterate(144, period);
    tick += static_cast<long long>(plan.step.steps);
    // What the player would see: the latest state minus the step still to come.
    const double position = double(tick) - 1.0 + plan.step.alpha;
    if (previous_position >= 0.0) {
      worst = std::max(worst, std::fabs((position - previous_position) - 60.0 / 144.0));
    }
    previous_position = position;
  }
  CHECK(worst < 1e-6);
}

void TestStallLeavesNoDebt() {
  std::printf("a stall does not leave interpolation behind\n");
  SimulationPacer pacer;
  pacer.Reset(InterpolationPolicy::kOn);
  double now = 1.0;
  pacer.Advance(At(now));
  for (int i = 0; i < 100; ++i) {
    now += 1.0 / 144.0;
    pacer.Advance(At(now));
  }
  now += 2.0;
  const StepPlan stall = pacer.Advance(At(now));
  CHECK(std::fabs(stall.steps - kMaxGameSteps) < kEps);
  CHECK(pacer.remainder() == 0.0);
  now += 1.0 / 144.0;
  const StepPlan after = pacer.Advance(At(now));
  CHECK(after.steps <= 1.0);
  CHECK(after.alpha >= 0.0 && after.alpha <= 1.0);
}

void TestClockNeverGoesBack() {
  std::printf("a clock that moves back is not a step\n");
  SimulationPacer pacer;
  pacer.Reset(InterpolationPolicy::kOn);
  pacer.Advance(At(10.0));
  pacer.Advance(At(10.01));
  const StepPlan back = pacer.Advance(At(9.0));
  CHECK(back.steps == 1.0);
  CHECK(back.alpha == 1.0);
  const StepPlan next = pacer.Advance(At(9.0 + 1.0 / 120.0));
  CHECK(next.steps <= 1.0);
  CHECK(pacer.remainder() >= 0.0 && pacer.remainder() < kStep);
}

void TestFirstFrameAfterReset() {
  std::printf("the first frame after a reset is one native step\n");
  Rig rig;
  rig.Settle(144, 1.0 / 144.0);
  // The pause menu: pacing ends, the scene object is replaced, pacing starts again after the settling period.
  rig.signals.paused = true;
  const IterationPlan paused = rig.Iterate(144, 1.0 / 144.0);
  CHECK(!paused.gameplay);
  CHECK(paused.ended);
  rig.signals.paused = false;
  rig.signals.scene = 2;
  for (int i = 0; i < GameplayGate::kEnterAfter - 1; ++i) {
    CHECK(!rig.Iterate(144, kStep).gameplay);
  }
  // A long load before the first paced frame is not caught up.
  const IterationPlan first = rig.Iterate(144, 1.5);
  CHECK(first.gameplay);
  CHECK(first.restarted);
  CHECK(first.step.steps == 1.0);
  CHECK(first.step.alpha == 1.0);
  CHECK(rig.stage.accumulator == 0.0f);
  const IterationPlan second = rig.Iterate(144, 1.0 / 144.0);
  CHECK(second.step.steps <= 1.0);
  CHECK(!second.restarted);
}

void TestSceneReset() {
  std::printf("a new scene object starts over\n");
  Rig rig;
  rig.Settle(144, 1.0 / 144.0);
  rig.signals.scene = 7;
  const IterationPlan changed = rig.Iterate(144, 1.0 / 144.0);
  CHECK(!changed.gameplay);
  CHECK(changed.ended);
  // The scene is not paced until it has run steadily again.
  int settled = 0;
  while (!rig.Iterate(144, kStep).gameplay) {
    ++settled;
    CHECK(settled < 100);
  }
  CHECK(settled == GameplayGate::kEnterAfter - 2);
}

void TestGate() {
  std::printf("gameplay gate\n");
  GameplayGate gate;
  GameplaySignals signals{true, true, false, 1};
  for (int i = 0; i < GameplayGate::kEnterAfter - 1; ++i) {
    CHECK(!gate.Next(signals));
  }
  CHECK(gate.Next(signals));
  // Frames without a tick keep it going once it has started.
  signals.stage_ticked = false;
  CHECK(gate.Next(signals));
  signals.paused = true;
  CHECK(!gate.Next(signals));
  signals.paused = false;
  signals.stage_ticked = true;
  CHECK(!gate.Next(signals));
  signals.stage_ran = false;
  CHECK(!gate.Next(signals));

  GameplayGate stalled;
  GameplaySignals unticked{true, false, false, 1};
  for (int i = 0; i < 200; ++i) {
    CHECK(!stalled.Next(unticked));
  }
}

void TestWholeSteps() {
  std::printf("whole steps for the once-per-step game functions\n");
  CHECK(WholeSteps(0.0) == 0);
  CHECK(WholeSteps(1.0) == 1);
  CHECK(WholeSteps(2.0) == 2);
  CHECK(WholeSteps(0.4) == 1);
  CHECK(WholeSteps(2.6) == 3);
  CHECK(WholeSteps(-1.0) == 0);
}

void TestWiring() {
  std::printf("frame schedule wiring\n");
  const std::string source = ReadSource("frame_schedule.cpp");
  CHECK(Has(source, "REX_HOOK_RAW(sub_82501D28)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824DC8C0)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_825182D0)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_82518BE8)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824E1590)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824EE340)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824F0188)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824E7A28)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_82503200)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_82427F88)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_8247FF00)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824808B0)"));
  CHECK(Has(source, "CatchUpMissedFrames(base, scene)"));

  const std::string scene = ReadSource("stage_scene.h");
  CHECK(Has(scene, "kSceneAccumulator = 68"));
  CHECK(Has(scene, "kSceneSpeed = 64"));
  CHECK(Has(scene, "kGameState = 0x82CC1EA0"));
  CHECK(Has(scene, "kTaskManagerPaused = 150"));
}

}  // namespace

int main() {
  TestStageModel();
  TestPresentationRates();
  TestThirtyFps();
  TestCountedFunctionsMatchScene();
  TestHitchKeepsInterpolation();
  TestOriginalRateIsUntouched();
  TestNearSixty();
  TestEarlyAndLateFrames();
  TestStallsAndBursts();
  TestRateTransitions();
  TestPolicyPerRate();
  TestMeasuredUnlimited();
  TestPausedAndReset();
  TestAlphaIsSteady();
  TestStallLeavesNoDebt();
  TestClockNeverGoesBack();
  TestFirstFrameAfterReset();
  TestSceneReset();
  TestGate();
  TestWholeSteps();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
