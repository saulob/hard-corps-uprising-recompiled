#pragma once

#include <bit>
#include <cstdint>

#include "guest_memory.h"

namespace hcu::stage {

// Stage scene: the speed added to the accumulator each frame, and the accumulator itself.
constexpr uint32_t kSceneSpeed = 64;
constexpr uint32_t kSceneAccumulator = 68;

// Task manager: the pause byte.
constexpr uint32_t kTaskManagerPaused = 150;

// Game state: the world is paused and the pause menu is open.
constexpr uint32_t kGameState = 0x82CC1EA0;
constexpr uint32_t kWorldPaused = 104;
constexpr uint32_t kPauseMenu = 108;

inline float LoadFloat(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(LoadBE32(base, addr));
}

inline void StoreFloat(uint8_t* base, uint32_t addr, float value) {
  StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
}

inline bool Paused(const uint8_t* base) {
  return LoadBE32(base, kGameState + kWorldPaused) != 0 || LoadBE32(base, kGameState + kPauseMenu) != 0;
}

}  // namespace hcu::stage
