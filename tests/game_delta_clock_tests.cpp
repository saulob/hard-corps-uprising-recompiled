// Low-FPS game speed timing tests: the delta clock's steady rates, jitter, bursts, cap and remainder, and the
// stage scene's step accumulator it drives. Pure logic plus source checks; the hook needs the running game.
// Run: hcu_game_delta_clock_tests

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game_delta_clock.h"

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
// How far the game's time may be from real time while no time is dropped.
constexpr double kLead = GameDeltaClock::kSnapTolerance * kStep;
constexpr double kEps = 1e-9;

double Near(double a, double b) { return std::fabs(a - b); }

SimulationClock::time_point At(double ns) {
  return SimulationClock::time_point{} + nanoseconds(static_cast<long long>(ns));
}

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// A run of the delta clock: real time in, simulated time out.
struct Run {
  double real = 0.0;
  double sim = 0.0;
  double drift = 0.0;
  double smallest = 1e9;
  double largest = 0.0;
};

template <class Next>
void Feed(GameDeltaClock& clock, Run& run, double seconds, Next next) {
  const double end = run.real + seconds;
  while (run.real < end) {
    const double interval = next();
    const double delta = clock.Take(interval, kStep);
    run.real += interval;
    run.sim += delta;
    run.drift = std::max(run.drift, std::fabs(run.sim - run.real));
    run.smallest = std::min(run.smallest, delta);
    run.largest = std::max(run.largest, delta);
  }
}

template <class Next>
Run Simulate(GameDeltaClock& clock, double seconds, Next next) {
  Run run;
  Feed(clock, run, seconds, next);
  return run;
}

bool KeepsTime(const Run& run) { return run.drift <= kLead + kEps; }

bool EveryDeltaIs(const Run& run, double delta) { return run.smallest == delta && run.largest == delta; }

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

// The stage scene: it adds its speed to the accumulator every frame and ticks once per whole unit. Frames without
// a step (paused, first after a reset) advance nothing.
struct Scene {
  GameplayDeltaClock clock;
  double ns = 0.0;
  double applied = 0.0;
  float accumulator = 0.0f;
  float speed = 1.0f;
  long long ticks = 0;
  long long frames = 0;
  double last_steps = 0.0;
  bool last_write = false;
  int largest = 0;

  int Frame(double delta_ns, bool paused = false) {
    ns += delta_ns;
    if (paused) {
      clock.Steps(At(ns), true);
      accumulator = 0.0f;
      last_steps = 0.0;
      last_write = false;
      return 0;
    }
    const double steps = clock.Steps(At(ns), false);
    last_steps = steps;
    last_write = false;
    if (steps > 0.0) {
      const float adjusted = AccumulatorForSteps(accumulator, speed, steps);
      last_write = adjusted != accumulator;
      accumulator = adjusted;
      applied += delta_ns;
    }
    accumulator += speed;
    const int ran = accumulator > 0.0f ? static_cast<int>(std::floor(accumulator)) : 0;
    accumulator -= static_cast<float>(ran);
    ticks += ran;
    ++frames;
    largest = std::max(largest, ran);
    return ran;
  }
};

// The game advanced about as much time as the scene ran for.
bool KeepsSceneTime(const Scene& scene) {
  return std::fabs(double(scene.ticks) * kStep - scene.applied * 1e-9) <= 2.0 * kStep + kEps;
}

// Stable frame spacing in nanoseconds.
double StepNs() { return kStep * 1e9; }

void TestClockSteady() {
  std::printf("delta clock at steady rates\n");
  for (const double fps : {60.0, 50.0, 45.0, 40.0, 35.0, 30.0, 20.0}) {
    GameDeltaClock clock;
    const Run run = Simulate(clock, 10.0, [&] { return 1.0 / fps; });
    CHECK(KeepsTime(run));
  }
  GameDeltaClock sixty;
  CHECK(EveryDeltaIs(Simulate(sixty, 10.0, [] { return kStep; }), kStep));
  GameDeltaClock thirty;
  CHECK(EveryDeltaIs(Simulate(thirty, 10.0, [] { return 2 * kStep; }), 2 * kStep));
  GameDeltaClock twenty;
  CHECK(EveryDeltaIs(Simulate(twenty, 10.0, [] { return 3 * kStep; }), 3 * kStep));
  // Intermediate rates fall between whole steps, so the interval itself is handed over.
  for (const double fps : {45.0, 40.0, 35.0}) {
    GameDeltaClock clock;
    const Run run = Simulate(clock, 10.0, [&] { return 1.0 / fps; });
    CHECK(run.smallest == run.largest);
    CHECK(Near(run.smallest, 1.0 / fps) < kEps);
  }
}

void TestClockJitter() {
  std::printf("delta clock with jitter\n");
  for (const double period : {kStep, 2 * kStep}) {
    GameDeltaClock clock;
    AnchoredTicks ticks(period, 0.5 * kLead);
    CHECK(EveryDeltaIs(Simulate(clock, 10.0, ticks), period));
  }
  for (const double period : {kStep, 2 * kStep}) {
    GameDeltaClock clock;
    AnchoredTicks ticks(period, 0.004);
    const Run run = Simulate(clock, 20.0, ticks);
    CHECK(KeepsTime(run));
  }
  // A frame rate a little under 60 keeps the native step and carries the slip.
  GameDeltaClock slipping;
  const Run slip = Simulate(slipping, 10.0, [] { return kStep * 1.003; });
  CHECK(KeepsTime(slip));
  CHECK(slip.smallest == kStep);
}

void TestClockTransitions() {
  std::printf("delta clock across rate changes\n");
  GameDeltaClock clock;
  Run run;
  for (const double fps : {30.0, 40.0, 45.0, 35.0, 30.0}) {
    Feed(clock, run, 4.0, [&] { return 1.0 / fps; });
    CHECK(Near(run.sim, run.real) <= kLead + kEps);
  }
  GameDeltaClock sudden;
  Run steps;
  Feed(sudden, steps, 2.0, [] { return kStep; });
  Feed(sudden, steps, 2.0, [] { return 2 * kStep; });
  Feed(sudden, steps, 2.0, [] { return kStep; });
  CHECK(KeepsTime(steps));
  CHECK(steps.smallest == kStep);
  CHECK(steps.largest == 2 * kStep);
}

void TestClockBursts() {
  std::printf("delta clock with bursts\n");
  const std::vector<std::vector<double>> patterns = {
      {0.044, 0.002, 0.002}, {0.008, 0.036}, {1.0 / 30, 0.002, 0.002, kStep}, {0.032, 0.003, 0.003, 0.003, 0.032}};
  for (const auto& pattern : patterns) {
    GameDeltaClock clock;
    CHECK(KeepsTime(Simulate(clock, 10.0, Cycle(pattern))));
  }
}

void TestClockCap() {
  std::printf("delta clock cap and long stalls\n");
  GameDeltaClock near_cap;
  CHECK(Near(near_cap.Take(0.049, kStep), 0.05) < 1e-6);
  GameDeltaClock over_cap;
  CHECK(Near(over_cap.Take(0.0501, kStep), 0.05) < 1e-6);
  CHECK(over_cap.owed() == 0.0);
  GameDeltaClock long_stall;
  CHECK(Near(long_stall.Take(30.0, kStep), 0.05) < 1e-6);
  CHECK(long_stall.owed() == 0.0);
  CHECK(long_stall.Take(kStep, kStep) == kStep);

  // A stall every twenty frames drops the excess each time instead of owing it.
  GameDeltaClock periodic;
  int frame = 0;
  int stalls = 0;
  const Run run = Simulate(periodic, 10.0, [&] {
    const bool stall = frame++ % 20 == 19;
    stalls += stall;
    return stall ? 0.07 : kStep;
  });
  CHECK(run.largest <= kMaxGameSteps * kStep + kEps);
  CHECK(std::fabs((run.real - run.sim) - stalls * 0.02) <= kLead + kEps);
}

void TestClockRemainder() {
  std::printf("delta clock remainder and reset\n");
  GameDeltaClock carried;
  CHECK(carried.Take(1.15 * kStep, kStep) == kStep);
  CHECK(Near(carried.owed() / kStep, 0.15) < kEps);
  CHECK(carried.Take(1.15 * kStep, kStep) > 1.25 * kStep);
  CHECK(std::fabs(carried.owed()) <= 0.5 * kStep + kEps);

  // The carried difference never leaves half a step over a long, varied run.
  GameDeltaClock bounded;
  bool in_bounds = true;
  uint32_t state = 7;
  for (int i = 0; i < 200000; ++i) {
    state = state * 1664525u + 1013904223u;
    const double factor = 0.7 + double(state >> 8) / double(1u << 24) * 1.8;
    bounded.Take(factor * kStep, kStep);
    in_bounds &= std::fabs(bounded.owed()) <= 0.5 * kStep + kEps;
  }
  CHECK(in_bounds);

  GameDeltaClock reset;
  reset.Take(1.15 * kStep, kStep);
  reset.Reset();
  CHECK(reset.owed() == 0.0);
  CHECK(reset.Take(1.15 * kStep, kStep) == kStep);
}

void TestSceneSteady() {
  std::printf("scene steps at steady rates\n");
  const struct {
    double fps;
    double ticks_per_frame;
    bool exact;
  } rates[] = {{60.0, 1.0, true}, {30.0, 2.0, true}, {20.0, 3.0, true},
               {45.0, 4.0 / 3.0, false}, {40.0, 1.5, false}, {35.0, 60.0 / 35.0, false}};
  for (const auto& rate : rates) {
    Scene scene;
    const double interval = 1e9 / rate.fps;
    for (int i = 0; i < 3000; ++i) {
      scene.Frame(interval);
    }
    CHECK(KeepsSceneTime(scene));
    const double per_frame = double(scene.ticks) / double(scene.frames);
    CHECK(Near(per_frame, rate.ticks_per_frame) < 0.01);
    if (rate.exact) {
      CHECK(scene.largest == static_cast<int>(rate.ticks_per_frame));
    } else {
      CHECK(scene.largest <= kMaxGameSteps);
    }
  }
}

void TestSceneNativeIsUnchanged() {
  std::printf("native 60 FPS keeps one step and writes nothing\n");
  Scene scene;
  const double interval = StepNs();
  // The first frame after gameplay starts has no previous frame and advances natively.
  CHECK(scene.Frame(interval) == 1);
  CHECK(scene.last_steps == 0.0);
  for (int i = 0; i < 5000; ++i) {
    CHECK(scene.Frame(interval) == 1);
    CHECK(scene.last_steps == 1.0);
    CHECK(!scene.last_write);
  }
  CHECK(scene.accumulator == 0.0f);

  // Jitter that stays within the snap tolerance still writes nothing.
  Scene jittered;
  for (int i = 0; i < 5000; ++i) {
    CHECK(jittered.Frame(i % 2 == 0 ? interval * 1.06 : interval * 0.94) == 1);
    CHECK(!jittered.last_write);
  }
}

void TestSceneTransitions() {
  std::printf("scene steps across rate changes\n");
  struct Case {
    double low_fps;
    int low_ticks;
  };
  for (const Case& c : {Case{30.0, 2}, Case{20.0, 3}}) {
    Scene scene;
    for (int i = 0; i < 600; ++i) {
      CHECK(scene.Frame(StepNs()) == 1);
    }
    for (int i = 0; i < 600; ++i) {
      scene.Frame(1e9 / c.low_fps);
    }
    for (int i = 0; i < 600; ++i) {
      CHECK(scene.Frame(StepNs()) == 1);
    }
    CHECK(KeepsSceneTime(scene));
    CHECK(scene.largest <= kMaxGameSteps);
    CHECK(scene.largest >= c.low_ticks);
  }
}

void TestSceneBursts() {
  std::printf("scene steps with short bursts\n");
  const std::vector<std::vector<double>> patterns = {
      {kStep * 2, kStep, kStep, kStep}, {kStep, kStep, kStep * 2, kStep, kStep * 3, kStep},
      {0.044, 0.002, 0.002, kStep, kStep}};
  for (const auto& pattern : patterns) {
    Scene scene;
    Cycle cycle(pattern);
    for (int i = 0; i < 3000; ++i) {
      CHECK(scene.Frame(cycle() * 1e9) <= kMaxGameSteps);
    }
    CHECK(KeepsSceneTime(scene));
  }
}

void TestSceneStalls() {
  std::printf("scene steps after stalls\n");
  Scene scene;
  for (int i = 0; i < 300; ++i) {
    scene.Frame(StepNs());
  }
  // One long stall advances only the cap and drops the rest.
  const int ticks = scene.Frame(1.5e9);
  CHECK(ticks == kMaxGameSteps);
  CHECK(scene.accumulator < 1.0f);
  CHECK(scene.Frame(StepNs()) == 1);

  // Repeated long stalls never owe time or run past the cap.
  Scene repeated;
  for (int i = 0; i < 50; ++i) {
    for (int j = 0; j < 5; ++j) {
      CHECK(repeated.Frame(StepNs()) == 1);
    }
    CHECK(repeated.Frame(0.4e9) == kMaxGameSteps);
    CHECK(repeated.accumulator < 1.0f);
  }
}

void TestScenePause() {
  std::printf("scene steps while paused\n");
  Scene scene;
  for (int i = 0; i < 120; ++i) {
    scene.Frame(StepNs());
  }
  const long long before = scene.ticks;
  for (int i = 0; i < 600; ++i) {
    CHECK(scene.Frame(StepNs(), true) == 0);
  }
  CHECK(scene.ticks == before);
  CHECK(scene.accumulator == 0.0f);
  // Resuming advances one step, never a catch-up burst.
  CHECK(scene.Frame(StepNs(), false) == 1);
  CHECK(scene.last_steps == 1.0);
}

void TestSceneReset() {
  std::printf("scene steps after a reset\n");
  Scene scene;
  // A rate that leaves a carried difference behind.
  for (int i = 0; i < 30; ++i) {
    scene.Frame(1e9 / 55.0);
  }
  scene.clock.Reset();
  CHECK(scene.Frame(StepNs()) == 1);
  CHECK(scene.last_steps == 0.0);
  CHECK(!scene.last_write);
  CHECK(scene.Frame(StepNs()) == 1);
}

void TestWiring() {
  std::printf("low-FPS game speed wiring\n");
  const std::string source = ReadSource("game_delta_clock.cpp");
  CHECK(Has(source, "REXCVAR_DEFINE_BOOL(hcu_low_fps_game_speed, true"));
  CHECK(Has(source, "REXCVAR_GET(hcu_low_fps_game_speed)"));
  CHECK(Has(source, "REX_HOOK_RAW(sub_824E1590)"));
  CHECK(Has(source, "kSceneAccumulator = 68"));
  CHECK(Has(source, "kSceneSpeed = 64"));
  CHECK(Has(source, "kGameState = 0x82CC1EA0"));

  // The rollback cvar restores the previous behaviour by skipping the adjustment entirely.
  CHECK(Has(source, "if (REXCVAR_GET(hcu_low_fps_game_speed)) {"));
}

}  // namespace

int main() {
  TestClockSteady();
  TestClockJitter();
  TestClockTransitions();
  TestClockBursts();
  TestClockCap();
  TestClockRemainder();
  TestSceneSteady();
  TestSceneNativeIsUnchanged();
  TestSceneTransitions();
  TestSceneBursts();
  TestSceneStalls();
  TestScenePause();
  TestSceneReset();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
