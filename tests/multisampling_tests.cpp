// Multisampling: host samples for each guest sample count and launcher choice, the Multisampling row,
// its settings key and where native_2x_msaa is set from it. The render target caches run only with a
// GPU, so their choice is mirrored by HostMsaaSamples.
// Run: hcu_multisampling_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "multisampling.h"

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

void TestHostSamples() {
  std::printf("host samples per launcher choice\n");
  for (bool supported : {false, true}) {
    for (bool compatibility : {false, true}) {
      bool native = hcu::Native2xMsaaFor(compatibility);
      CHECK(hcu::HostMsaaSamples(1, native, supported) == 1);
      CHECK(hcu::HostMsaaSamples(4, native, supported) == 4);
    }
  }
  CHECK(hcu::HostMsaaSamples(2, hcu::Native2xMsaaFor(false), true) == 2);
  CHECK(hcu::HostMsaaSamples(2, hcu::Native2xMsaaFor(true), true) == 4);
  CHECK(hcu::HostMsaaSamples(2, hcu::Native2xMsaaFor(false), false) == 4);
  // No choice draws fewer host samples than the game asks for, and native is never more.
  for (bool supported : {false, true}) {
    CHECK(hcu::HostMsaaSamples(2, hcu::Native2xMsaaFor(true), supported) >= 2);
    CHECK(hcu::HostMsaaSamples(2, hcu::Native2xMsaaFor(false), supported) <=
          hcu::HostMsaaSamples(2, hcu::Native2xMsaaFor(true), supported));
  }
}

void TestSetting() {
  std::printf("setting, launcher row and startup\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(hcu_msaa_compatibility, false,"));
  CHECK(Has(settings, "    \"hcu_msaa_compatibility\",\n") ||
        Has(settings, "    \"hcu_msaa_compatibility\",\r\n"));
  CHECK(!Has(settings, "    \"native_2x_msaa\","));
  CHECK(Has(settings, "  ApplyInternalRenderPath();\n  ApplyMultisampling();") ||
        Has(settings, "  ApplyInternalRenderPath();\r\n  ApplyMultisampling();"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"native_2x_msaa\""));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "\"hcu_msaa_compatibility\", false, \"2\\xC3\\x97 MSAA\", \"4\\xC3\\x97 MSAA (Compatibility)\""));
  CHECK(!Has(launcher, "\"native_2x_msaa\""));

  const std::string app = ReadSource("hard_corps_uprising_app.h");
  const std::size_t no_launcher = app.find("if (!show_launcher_) {\n      hcu::ApplyMultisampling();");
  const std::size_t no_launcher_crlf =
      app.find("if (!show_launcher_) {\r\n      hcu::ApplyMultisampling();");
  CHECK(no_launcher != std::string::npos || no_launcher_crlf != std::string::npos);
}

}  // namespace

int main() {
  TestHostSamples();
  TestSetting();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
