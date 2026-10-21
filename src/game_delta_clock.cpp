#include "game_delta_clock.h"

#include <rex/cvar.h>

#include "stage_scene.h"

REXCVAR_DEFINE_BOOL(hcu_low_fps_game_speed, true, "Hard Corps",
                    "Keep gameplay at its intended speed when frames miss the 60 Hz deadline");

namespace hcu {
namespace {

GameplayDeltaClock g_clock;

}  // namespace

// A frame that missed the 60 Hz deadline leaves the scene's accumulator owing whole steps of game time. Writing
// them before the scene runs makes its tick loop advance once per owed step; on time, the value is unchanged.
void CatchUpMissedFrames(uint8_t* base, uint32_t scene) {
  if (!REXCVAR_GET(hcu_low_fps_game_speed)) {
    return;
  }
  const double steps = g_clock.Steps(SimulationClock::now(), stage::Paused(base), scene);
  if (steps <= 0.0) {
    return;
  }
  const float accumulator = stage::LoadFloat(base, scene + stage::kSceneAccumulator);
  const float adjusted =
      AccumulatorForSteps(accumulator, stage::LoadFloat(base, scene + stage::kSceneSpeed), steps);
  if (adjusted != accumulator) {
    stage::StoreFloat(base, scene + stage::kSceneAccumulator, adjusted);
  }
}

void ResetCatchUp() {
  g_clock.Reset();
}

}  // namespace hcu
