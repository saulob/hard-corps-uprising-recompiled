#include "game_delta_clock.h"

#include <bit>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "guest_memory.h"

REXCVAR_DEFINE_BOOL(hcu_low_fps_game_speed, true, "Hard Corps",
                    "Keep gameplay at its intended speed when frames miss the 60 Hz deadline");

namespace hcu {
namespace {

// Stage scene: the speed added to the accumulator each frame, and the accumulator itself.
constexpr uint32_t kSceneSpeed = 64;
constexpr uint32_t kSceneAccumulator = 68;

// Game state: the world is paused and the pause menu is open.
constexpr uint32_t kGameState = 0x82CC1EA0;
constexpr uint32_t kWorldPaused = 104;
constexpr uint32_t kPauseMenu = 108;

GameplayDeltaClock g_clock;

float LoadFloat(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(LoadBE32(base, addr));
}

void StoreFloat(uint8_t* base, uint32_t addr, float value) {
  StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
}

bool Paused(const uint8_t* base) {
  return LoadBE32(base, kGameState + kWorldPaused) != 0 || LoadBE32(base, kGameState + kPauseMenu) != 0;
}

// A frame that missed the 60 Hz deadline leaves the scene's accumulator owing whole steps of game time. Writing
// them before the scene runs makes its tick loop advance once per owed step; on time, the value is unchanged.
void CatchUpMissedFrames(uint8_t* base, uint32_t scene) {
  const double steps = g_clock.Steps(SimulationClock::now(), Paused(base), scene);
  if (steps <= 0.0) {
    return;
  }
  const float accumulator = LoadFloat(base, scene + kSceneAccumulator);
  const float adjusted = AccumulatorForSteps(accumulator, LoadFloat(base, scene + kSceneSpeed), steps);
  if (adjusted != accumulator) {
    StoreFloat(base, scene + kSceneAccumulator, adjusted);
  }
}

}  // namespace
}  // namespace hcu

// The stage scene's frame (task slot 8): its accumulator drives the simulation steps.
REX_EXTERN(__imp__sub_824E1590);
REX_HOOK_RAW(sub_824E1590) {
  if (REXCVAR_GET(hcu_low_fps_game_speed)) {
    hcu::CatchUpMissedFrames(base, ctx.r3.u32);
  }
  __imp__sub_824E1590(ctx, base);
}
