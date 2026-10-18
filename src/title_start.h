#pragma once

#include <cstdint>

namespace rex::input {
class InputSystem;
}

namespace hcu {

// The game's cooked input bit for START on the title screen. This is not
// XInput's 0x0010 bit; the title's input manager uses bit 3.
inline constexpr uint32_t kTitleStartButton = 0x8;

// Title object byte that is zero while the "Press Start" window is on screen
// and non-zero once it has been dismissed or the attract mode took over.
inline constexpr uint32_t kPressStartFlagOffset = 57;

constexpr bool PressStartWindowActive(uint32_t title_object, uint8_t press_start_flag) {
  return title_object != 0 && press_start_flag == 0;
}

// The title checks START for every guest user, but the input manager only keeps
// the active user's cached state current. While the "Press Start" window is up,
// let the live state of the user being queried answer the check, so the title
// still records which controller pressed START. No device assignment is changed.
constexpr bool ShouldOverrideTitleStart(bool title_update_active, bool press_start_window,
                                        uint32_t user, uint32_t button,
                                        uint32_t start_user_mask) {
  if (!title_update_active || !press_start_window || button != kTitleStartButton) {
    return false;
  }
  if (user == 0xFFFFFFFFu) {
    return start_user_mask != 0;
  }
  return user < 32 && (start_user_mask & (1u << user)) != 0;
}

// The app supplies the input system so the title path can query users 0..3
// without changing their device assignment.
void SetTitleStartInputSystem(rex::input::InputSystem* input_system);

}  // namespace hcu
