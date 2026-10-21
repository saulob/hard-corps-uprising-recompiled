// Runs gameplay at the original 60 Hz under a selected frame rate: each main-loop iteration is given a number of
// simulation steps, the game functions that count frames run once per step, and the draw interpolates between steps.

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>

#include "frame_limiter.h"
#include "game_delta_clock.h"
#include "simulation_pacer.h"
#include "stage_scene.h"
#include "visual_interpolation.h"

namespace hcu {
namespace {

// The objects a once-per-iteration function has already been given its steps for in this iteration.
struct Handled {
  std::array<uint32_t, 4> objects{};
  size_t count = 0;

  bool Take(uint32_t object) {
    for (size_t i = 0; i < count; ++i) {
      if (objects[i] == object) {
        return false;
      }
    }
    if (count < objects.size()) {
      objects[count++] = object;
    }
    return true;
  }
  void Clear() { count = 0; }
};

struct PadState {
  bool valid = false;
  uint64_t result = 0;
  std::array<uint8_t, 16> bytes{};
};

// The main loop, the scene's frame and the hooks below all run on the game thread.
struct Schedule {
  IterationSchedule iterations;
  RenderInterpolator render;
  GameplaySignals seen;
  bool task_paused = false;
  bool active = false;
  int whole_steps = 1;
  double steps = 1.0;
  // Each once-per-iteration game function runs once per step of the iteration, for each object it is called on.
  Handled loop;
  Handled session;
  Handled update;
  Handled run;
  int logged_rate = -1;
  int logged_interpolation = -1;
};

Schedule g;
std::array<PadState, 4> g_pads;
std::atomic<LimiterClock::rep> g_hold_input_until{0};

constexpr auto kInputHold = std::chrono::milliseconds(100);

GameplaySignals TakeSignals(const uint8_t* base) {
  GameplaySignals signals = g.seen;
  signals.paused = g.task_paused || stage::Paused(base);
  g.seen = {};
  g.task_paused = false;
  return signals;
}

void LogPacing(int configured, const IterationPlan& plan) {
  if (!plan.gameplay) {
    return;
  }
  const int interpolating = g.iterations.pacer().interpolating() ? 1 : 0;
  if (configured == g.logged_rate && interpolating == g.logged_interpolation) {
    return;
  }
  g.logged_rate = configured;
  g.logged_interpolation = interpolating;
  REXLOG_INFO("Runtime frame pacing: frame rate={} | simulation=60Hz | visual interpolation={}",
              FrameRateLabel(configured), interpolating ? "active" : "inactive");
}

void BeginIteration(const uint8_t* base) {
  const int configured = ConfiguredFrameRate();
  const IterationPlan plan = g.iterations.Next(configured, TakeSignals(base), SimulationClock::now());
  SetGameplayPaced(plan.gameplay);
  if (plan.restarted || plan.ended) {
    ResetCatchUp();
  }
  g.active = plan.gameplay;
  g.steps = plan.step.steps;
  g.whole_steps = WholeSteps(plan.step.steps);
  g.render.BeginIteration(plan.gameplay, plan.step.interpolate, plan.step.alpha, plan.restarted || plan.ended);
  g.loop.Clear();
  g.session.Clear();
  g.update.Clear();
  g.run.Clear();
  LogPacing(configured, plan);

  // A pad read in an iteration without a step would take the press from the next step's edge detection.
  const auto hold_until = plan.gameplay && g.whole_steps == 0
                              ? (LimiterClock::now() + kInputHold).time_since_epoch()
                              : LimiterClock::duration::zero();
  g_hold_input_until.store(hold_until.count(), std::memory_order_relaxed);
}

// Runs a once-per-iteration game function once per simulation step of this iteration.
void RunPerStep(PPCContext& ctx, uint8_t* base, Handled& handled, PPCFunc* original) {
  if (!g.active || !handled.Take(ctx.r3.u32)) {
    original(ctx, base);
    return;
  }
  const uint64_t r3 = ctx.r3.u64;
  for (int i = 0; i < g.whole_steps; ++i) {
    ctx.r3.u64 = r3;
    original(ctx, base);
  }
}

// The scene ticks once per unit of its accumulator, which it fills with its speed on every call.
void ShapeSceneSteps(uint8_t* base, uint32_t scene) {
  const float speed = stage::LoadFloat(base, scene + stage::kSceneSpeed);
  const float accumulator = stage::LoadFloat(base, scene + stage::kSceneAccumulator);
  stage::StoreFloat(base, scene + stage::kSceneAccumulator, AccumulatorForSteps(accumulator, speed, g.steps));
}

void RunTick(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  g.render.CountTick();
  g.seen.stage_ticked = true;
  const bool outer = g.render.EnterTick();
  original(ctx, base);
  g.render.LeaveTick(outer);
}

void RunStage(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t scene = ctx.r3.u32;
  g.seen.stage_ran = true;
  g.seen.scene = scene;
  if (!g.active) {
    CatchUpMissedFrames(base, scene);
    original(ctx, base);
    return;
  }
  if (g.run.Take(scene)) {
    ShapeSceneSteps(base, scene);
  }
  g.render.BeginWindow();
  original(ctx, base);
  g.render.EndWindow(base);
}

}  // namespace
}  // namespace hcu

// Start of each main-loop iteration: XNotify messages and the game's frame-counted timer queue.
REX_EXTERN(__imp__sub_82501D28);
REX_HOOK_RAW(sub_82501D28) {
  hcu::BeginIteration(base);
  hcu::RunPerStep(ctx, base, hcu::g.loop, __imp__sub_82501D28);
}

// Session system tick, once per main-loop iteration. Its counters are frame-based.
REX_EXTERN(__imp__sub_824DC8C0);
REX_HOOK_RAW(sub_824DC8C0) {
  hcu::RunPerStep(ctx, base, hcu::g.session, __imp__sub_824DC8C0);
}

// Task manager update pass.
REX_EXTERN(__imp__sub_825182D0);
REX_HOOK_RAW(sub_825182D0) {
  if (base[ctx.r3.u32 + hcu::stage::kTaskManagerPaused] != 0) {
    hcu::g.task_paused = true;
  }
  hcu::RunPerStep(ctx, base, hcu::g.update, __imp__sub_825182D0);
}

// Task manager draw pass. The stage's background is drawn here with the scene camera.
REX_EXTERN(__imp__sub_82518BE8);
REX_HOOK_RAW(sub_82518BE8) {
  if (!hcu::g.active) {
    __imp__sub_82518BE8(ctx, base);
    return;
  }
  hcu::g.render.BeginWindow();
  __imp__sub_82518BE8(ctx, base);
  hcu::g.render.EndWindow(base);
}

// The stage scene's frame (task slot 8): its accumulator ticks the simulation, then the scene draws.
REX_EXTERN(__imp__sub_824E1590);
REX_HOOK_RAW(sub_824E1590) {
  hcu::RunStage(ctx, base, __imp__sub_824E1590);
}

// The scene's tick, one implementation per scene class.
REX_EXTERN(__imp__sub_824EE340);
REX_HOOK_RAW(sub_824EE340) {
  hcu::RunTick(ctx, base, __imp__sub_824EE340);
}

REX_EXTERN(__imp__sub_824F0188);
REX_HOOK_RAW(sub_824F0188) {
  hcu::RunTick(ctx, base, __imp__sub_824F0188);
}

// The scene's second per-tick step reads the camera's position, which has to be the game's.
REX_EXTERN(__imp__sub_824E7A28);
REX_HOOK_RAW(sub_824E7A28) {
  const bool outer = hcu::g.render.EnterTick();
  hcu::g.render.SuspendDrawn(base);
  __imp__sub_824E7A28(ctx, base);
  hcu::g.render.ResumeDrawn(base);
  hcu::g.render.LeaveTick(outer);
}

// Camera::Apply(camera).
REX_EXTERN(__imp__sub_82503200);
REX_HOOK_RAW(sub_82503200) {
  const uint32_t camera = ctx.r3.u32;
  __imp__sub_82503200(ctx, base);
  hcu::g.render.CameraApplied(base, camera, [&] {
    ctx.r3.u64 = camera;
    __imp__sub_82503200(ctx, base);
  });
}

// Sprite object world matrix: (object, matrix, camera).
REX_EXTERN(__imp__sub_82427F88);
REX_HOOK_RAW(sub_82427F88) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t matrix = ctx.r4.u32;
  __imp__sub_82427F88(ctx, base);
  hcu::g.render.ObjectMatrix(base, object, matrix);
}

// Model object draws (object, drawer). Their world matrix was built by the update.
REX_EXTERN(__imp__sub_8247FF00);
REX_HOOK_RAW(sub_8247FF00) {
  hcu::g.render.ObjectMatrix(base, ctx.r3.u32, ctx.r3.u32 + hcu::render_layout::kObjectWorldMatrix);
  __imp__sub_8247FF00(ctx, base);
}

REX_EXTERN(__imp__sub_824808B0);
REX_HOOK_RAW(sub_824808B0) {
  hcu::g.render.ObjectMatrix(base, ctx.r3.u32, ctx.r3.u32 + hcu::render_layout::kObjectWorldMatrix);
  __imp__sub_824808B0(ctx, base);
}

// XInputGetState(user, state). An iteration without a step repeats the last state the game read.
REX_EXTERN(__imp__sub_82B71170);
REX_HOOK_RAW(sub_82B71170) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  if (user >= hcu::g_pads.size() || state == 0) {
    __imp__sub_82B71170(ctx, base);
    return;
  }
  hcu::PadState& pad = hcu::g_pads[user];
  const auto hold_until = hcu::g_hold_input_until.load(std::memory_order_relaxed);
  if (pad.valid && hcu::LimiterClock::now().time_since_epoch().count() < hold_until) {
    std::memcpy(base + state, pad.bytes.data(), pad.bytes.size());
    ctx.r3.u64 = pad.result;
    return;
  }
  __imp__sub_82B71170(ctx, base);
  pad.valid = true;
  pad.result = ctx.r3.u64;
  std::memcpy(pad.bytes.data(), base + state, pad.bytes.size());
}
