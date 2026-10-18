// Title-screen START rule: while the "Press Start" window is up, a live START on
// the queried guest user answers the title's START query, so the title still
// records which controller pressed START. No device assignment is changed.
// Run: hcu_title_start_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "title_start.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void TestPressStartWindow() {
  std::printf("the Press Start window is only active while its flag is zero\n");
  CHECK(hcu::kPressStartFlagOffset == 57);
  CHECK(hcu::PressStartWindowActive(0x1234, 0));
  CHECK(!hcu::PressStartWindowActive(0x1234, 1));
  CHECK(!hcu::PressStartWindowActive(0x1234, 2));
  CHECK(!hcu::PressStartWindowActive(0, 0));
}

void TestPerUserRule() {
  std::printf("only the queried user's cooked START bit is overridden\n");
  constexpr uint32_t kUser1Start = 1u << 1;
  CHECK(hcu::kTitleStartButton == 0x8);
  CHECK(hcu::ShouldOverrideTitleStart(true, true, 1, hcu::kTitleStartButton, kUser1Start));
  CHECK(!hcu::ShouldOverrideTitleStart(false, true, 1, hcu::kTitleStartButton, kUser1Start));
  CHECK(!hcu::ShouldOverrideTitleStart(true, false, 1, hcu::kTitleStartButton, kUser1Start));
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 0, hcu::kTitleStartButton, kUser1Start));
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 2, hcu::kTitleStartButton, kUser1Start));
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 1, 0x10, kUser1Start));   // XInput START
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 1, 0x4000, kUser1Start)); // A
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 1, 0x40, kUser1Start));   // DOWN
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 1, hcu::kTitleStartButton, 0));
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 32, hcu::kTitleStartButton, 0xFFFFFFFFu));

  // The manager also uses -1 ("any user") internally.
  CHECK(hcu::ShouldOverrideTitleStart(true, true, 0xFFFFFFFFu, hcu::kTitleStartButton,
                                      kUser1Start));
  CHECK(!hcu::ShouldOverrideTitleStart(true, true, 0xFFFFFFFFu, hcu::kTitleStartButton, 0));
}

void TestWiring() {
  std::printf("hooks and app wiring use the title-only path\n");
  const std::string hook = ReadSource("title_start.cpp");
  CHECK(hook.find("REX_HOOK_RAW(sub_824F5218)") != std::string::npos);
  CHECK(hook.find("REX_HOOK_RAW(sub_8250E918)") != std::string::npos);
  CHECK(hook.find("ShouldOverrideTitleStart") != std::string::npos);
  CHECK(hook.find("PressStartWindowActive") != std::string::npos);
  CHECK(hook.find("UsersWithStart") != std::string::npos);
  CHECK(hook.find("sub_82B71170") == std::string::npos);
  CHECK(hook.find("SetDeviceAssignment") == std::string::npos);
  CHECK(hook.find("SharedAssignment") == std::string::npos);

  const std::string app = ReadSource("hard_corps_uprising_app.h");
  CHECK(app.find("SetTitleStartInputSystem(input)") != std::string::npos);
  CHECK(app.find("SetDeviceAssignment") == std::string::npos);
  CHECK(app.find("SharedAssignment") == std::string::npos);
}

}  // namespace

int main() {
  TestPressStartWindow();
  TestPerUserRule();
  TestWiring();
  std::printf("%s: %d of %d checks failed\n", g_failures ? "FAIL" : "PASS", g_failures, g_checks);
  return g_failures ? 1 : 0;
}
