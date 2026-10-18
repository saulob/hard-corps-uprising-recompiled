// Control scheme: the key options of each scheme, the scheme a set of key rows names, and the
// controller settings window hook. The mapping is pure; the hook is checked from source because the
// window needs the running game.
// Run: hcu_control_scheme_tests

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "control_scheme.h"

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

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestSchemeTables() {
  std::printf("control scheme tables\n");
  CHECK(hcu::kControlSchemeCount == 3);
  CHECK(hcu::kControlKeyRowCount == 10);
  // Every scheme keeps a full set of key options and the three differ from each other.
  for (uint32_t a = 0; a < hcu::kControlSchemeCount; ++a) {
    for (uint32_t row = 0; row < hcu::kControlKeyRowCount; ++row) {
      CHECK(hcu::kControlSchemeKeys[a][row] < 8);
    }
    for (uint32_t b = a + 1; b < hcu::kControlSchemeCount; ++b) {
      CHECK(hcu::kControlSchemeKeys[a] != hcu::kControlSchemeKeys[b]);
    }
  }
}

void TestMatching() {
  std::printf("scheme of a set of key rows\n");
  for (uint32_t scheme = 0; scheme < hcu::kControlSchemeCount; ++scheme) {
    CHECK(hcu::ControlSchemeForKeys(hcu::kControlSchemeKeys[scheme]) == scheme);
  }

  // A customized key no longer names any scheme, and reads as Basic.
  hcu::ControlKeyOptions keys = hcu::kControlSchemeKeys[1];
  keys[4] = keys[4] == 0 ? 1 : 0;
  CHECK(hcu::ControlSchemeForKeys(keys) == 0);

  CHECK(hcu::ControlSchemeForKeys(hcu::ControlKeyOptions{}) == 0);
  hcu::ControlKeyOptions all_seven{};
  all_seven.fill(7);
  CHECK(hcu::ControlSchemeForKeys(all_seven) == 0);
}

void TestWindowHook() {
  std::printf("controller settings window hook\n");
  const std::string hook = ReadSource("control_scheme.cpp");
  CHECK(Has(hook, "REX_EXTERN(__imp__sub_82535F00);"));
  CHECK(Has(hook, "REX_HOOK_RAW(sub_82535F00) {"));
  CHECK(Has(hook, "REX_IMPORT(__imp__sub_8253CA90, SetRowOption, void(u32, u32));"));

  const std::size_t original = hook.find("__imp__sub_82535F00(ctx, base);");
  const std::size_t restore = hook.find("SetRowOption(scheme_row, hcu::ControlSchemeForKeys(keys));");
  CHECK(original != std::string::npos);
  CHECK(restore != std::string::npos);
  CHECK(original < restore);  // the window is built first, then the scheme row is restored

  // The row pointers the hook reads are the ones the constructor writes.
  CHECK(Has(hook, "constexpr uint32_t kPlayerStride = 44;"));
  CHECK(Has(hook, "constexpr uint32_t kFirstKeyRow = 32;"));
  CHECK(Has(hook, "constexpr uint32_t kSchemeRow = 72;"));
  CHECK(Has(hook, "constexpr uint32_t kRowOption = 32;"));
}

}  // namespace

int main() {
  TestSchemeTables();
  TestMatching();
  TestWindowHook();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
