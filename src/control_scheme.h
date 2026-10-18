#pragma once

#include <array>
#include <cstdint>

namespace hcu {

// The controller settings window has one row per control plus a row that names the scheme the key rows
// came from. The game lists Basic, Shooter and Arcade in that order.
inline constexpr uint32_t kControlSchemeCount = 3;
inline constexpr uint32_t kControlKeyRowCount = 10;
inline constexpr uint32_t kControlPlayerCount = 2;

// The option each key row holds for a scheme, in key row order (the game's own preset tables).
using ControlKeyOptions = std::array<uint32_t, kControlKeyRowCount>;

inline constexpr ControlKeyOptions kControlSchemeKeys[kControlSchemeCount] = {
    ControlKeyOptions{2, 3, 4, 5, 6, 6, 0, 1, 7, 7},  // Basic
    ControlKeyOptions{2, 3, 5, 7, 0, 5, 1, 4, 7, 6},  // Shooter
    ControlKeyOptions{4, 2, 5, 3, 7, 7, 0, 1, 7, 7},  // Arcade
};

// The scheme whose key options are exactly these, or Basic when the player changed keys by hand.
inline uint32_t ControlSchemeForKeys(const ControlKeyOptions& keys) {
  for (uint32_t scheme = 1; scheme < kControlSchemeCount; ++scheme) {
    if (keys == kControlSchemeKeys[scheme]) {
      return scheme;
    }
  }
  return 0;
}

}  // namespace hcu
