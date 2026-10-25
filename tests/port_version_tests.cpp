// Port version: built from the numeric project version components. Run: hcu_port_version_tests

#include <cstdio>
#include <string>

#include "port_version.h"

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

}  // namespace

int main() {
  CHECK(hcu::PortVersion() == HCU_EXPECTED_PORT_VERSION);
  CHECK(&hcu::PortVersion() == &hcu::PortVersion());
  CHECK("v" + hcu::PortVersion() == "v" HCU_EXPECTED_PORT_VERSION);
  std::printf("PortVersion %s\n", hcu::PortVersion().c_str());
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
