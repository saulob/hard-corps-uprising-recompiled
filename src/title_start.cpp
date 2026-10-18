#include "title_start.h"

#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>

namespace {

constexpr uint32_t kGuestUserCount = 4;

std::atomic<rex::input::InputSystem*> g_input_system{nullptr};
thread_local int g_title_update_depth = 0;
thread_local bool g_press_start_window = false;
thread_local uint32_t g_start_users = 0;

bool TitleUpdateActive() { return g_title_update_depth > 0; }

uint32_t UsersWithStart(rex::input::InputSystem* input_system) {
  uint32_t mask = 0;
  if (!input_system) {
    return mask;
  }
  for (uint32_t user = 0; user < kGuestUserCount; ++user) {
    rex::input::X_INPUT_STATE state{};
    if (input_system->GetState(user, &state) != 0) {
      continue;
    }
    if ((uint16_t(state.gamepad.buttons) & rex::input::X_INPUT_GAMEPAD_START) != 0) {
      mask |= 1u << user;
    }
  }
  return mask;
}

}  // namespace

void hcu::SetTitleStartInputSystem(rex::input::InputSystem* input_system) {
  g_input_system.store(input_system, std::memory_order_release);
}

// sub_824F5218 is the title screen's update. It runs the "Press Start" check.
REX_EXTERN(__imp__sub_824F5218);
REX_HOOK_RAW(sub_824F5218) {
  const uint32_t title_object = ctx.r3.u32;
  uint8_t press_start_flag = 0;
  if (title_object != 0) {
    press_start_flag = base[title_object + hcu::kPressStartFlagOffset];
  }

  ++g_title_update_depth;
  const bool previous_window = g_press_start_window;
  const uint32_t previous_start_users = g_start_users;
  g_press_start_window = hcu::PressStartWindowActive(title_object, press_start_flag);
  g_start_users =
      g_press_start_window
          ? UsersWithStart(g_input_system.load(std::memory_order_acquire))
          : 0;
  __imp__sub_824F5218(ctx, base);
  g_start_users = previous_start_users;
  g_press_start_window = previous_window;
  --g_title_update_depth;
}

// sub_8250E918 is the input manager's "is button down" query. The title calls
// it for START on every user, but only the active user's cached state is fresh.
// While the "Press Start" window is up, answer with the live state of the user
// being queried, so the title still knows which controller pressed START.
REX_EXTERN(__imp__sub_8250E918);
REX_HOOK_RAW(sub_8250E918) {
  if (hcu::ShouldOverrideTitleStart(TitleUpdateActive(), g_press_start_window,
                                    ctx.r4.u32, ctx.r5.u32, g_start_users)) {
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_8250E918(ctx, base);
}
