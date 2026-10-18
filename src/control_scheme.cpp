#include "control_scheme.h"

#include <rex/hook.h>

#include "guest_memory.h"

namespace {

// Offsets of the controller settings window, from the recompiled constructor sub_82535F00: a 44-byte
// block per player holds the pointers of the ten key rows and then the pointer of the scheme row.
constexpr uint32_t kPlayerStride = 44;
constexpr uint32_t kFirstKeyRow = 32;
constexpr uint32_t kSchemeRow = 72;
constexpr uint32_t kRowOption = 32;

}  // namespace

// sub_8253CA90(row, option): the game's own row setter, which also updates the option that is shown.
REX_IMPORT(__imp__sub_8253CA90, SetRowOption, void(u32, u32));

// The window constructor loads the saved key rows but never restores the scheme row, so it always reads
// Basic even when the keys came from Shooter or Arcade. Restore it from the keys that were loaded.
REX_EXTERN(__imp__sub_82535F00);
REX_HOOK_RAW(sub_82535F00) {
  const uint32_t window = ctx.r3.u32;
  __imp__sub_82535F00(ctx, base);

  for (uint32_t player = 0; player < hcu::kControlPlayerCount; ++player) {
    const uint32_t rows = window + kFirstKeyRow + player * kPlayerStride;
    hcu::ControlKeyOptions keys{};
    bool complete = true;
    for (uint32_t row = 0; row < hcu::kControlKeyRowCount; ++row) {
      const uint32_t object = hcu::LoadBE32(base, rows + 4 * row);
      if (object == 0) {
        complete = false;
        break;
      }
      keys[row] = hcu::LoadBE32(base, object + kRowOption);
    }
    if (!complete) {
      continue;
    }

    const uint32_t scheme_row = hcu::LoadBE32(base, window + kSchemeRow + player * kPlayerStride);
    if (scheme_row != 0) {
      SetRowOption(scheme_row, hcu::ControlSchemeForKeys(keys));
    }
  }
}
