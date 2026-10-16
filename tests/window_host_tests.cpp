// Cursor auto-hide and window title tests: the state machine with synthetic time, the title text and its wiring.
// The window calls need the running game.
// Run: hcu_window_host_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "aspect_ratio.h"
#include "window_host.h"
#include "window_title.h"

using namespace hcu;

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

void TestCursorHider() {
  std::printf("cursor hider\n");
  using Clock = CursorHider::Clock;
  const auto at = [](int ms) { return Clock::time_point{} + std::chrono::milliseconds(ms); };
  const std::optional<bool> hide = false, show = true;

  CursorHider overlay;
  CHECK(!overlay.Update(false, true, true, at(0)) && !overlay.Update(false, true, true, at(60000)));
  CursorHider windowed;
  CHECK(!windowed.Update(true, false, true, at(0)) && !windowed.Update(true, false, true, at(60000)));

  CursorHider c;
  CHECK(!c.Update(true, true, true, at(0)) && c.visible());
  CHECK(!c.Update(true, true, true, at(4999)));
  CHECK(c.Update(true, true, true, at(5000)) == hide);
  CHECK(!c.Update(true, true, true, at(5100)) && !c.Update(true, true, true, at(9000)));

  c.MouseMoved(at(6000));
  CHECK(c.Update(true, true, true, at(6000)) == show);
  c.MouseMoved(at(8000));
  CHECK(!c.Update(true, true, true, at(12999)) && c.Update(true, true, true, at(13000)) == hide);

  CHECK(c.Update(true, false, true, at(13100)) == show);
  CHECK(!c.Update(true, true, true, at(20000)) && !c.Update(true, true, true, at(24999)));
  CHECK(c.Update(true, true, true, at(25000)) == hide);

  CHECK(c.Update(true, true, false, at(25100)) == show);
  CHECK(!c.Update(true, true, true, at(30000)) && !c.Update(true, true, true, at(34999)));
  CHECK(c.Update(true, true, true, at(35000)) == hide);

  c.MouseMoved(at(36000));
  CHECK(c.Update(true, true, true, at(36000)) == show);
  CHECK(c.Update(true, true, true, at(41000)) == hide);
  CHECK(c.Update(false, true, true, at(41100)) == show);
  CHECK(!c.Update(false, true, true, at(99000)) && c.visible());
}

void TestWindowTitle() {
  std::printf("window title\n");
  const std::string game = kWindowTitle;
  const std::optional<std::string> resolution = "3360x1440";
  CHECK(WindowTitleText(std::nullopt, std::nullopt) == game);
  CHECK(WindowTitleText(resolution, std::nullopt) == game + " | 3360x1440");
  CHECK(WindowTitleText(std::nullopt, 60.0) == game + " | FPS: 60.00");
  CHECK(WindowTitleText(resolution, 60.0) == game + " | 3360x1440 | FPS: 60.00");
  CHECK(WindowTitleText(resolution, 59.94) == game + " | 3360x1440 | FPS: 59.94");
}

void TestRenderResolution() {
  std::printf("render resolution text\n");
  CHECK(RenderResolutionText(1, AspectRatio::k21x9) == "1680x720");
  CHECK(RenderResolutionText(2, AspectRatio::k21x9) == "3360x1440");
  CHECK(RenderResolutionText(3, AspectRatio::k21x9) == "5040x2160");
  CHECK(RenderResolutionText(4, AspectRatio::k21x9) == "6720x2880");
  CHECK(RenderResolutionText(2, AspectRatio::k16x9) == "2560x1440");
}

void TestWiring() {
  std::printf("render resolution title wiring\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(hcu_show_render_resolution, false"));
  CHECK(Has(settings, "\"hcu_show_render_resolution\","));  // saved and reset with the other game settings

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "\"hcu_show_render_resolution\", false, \"Off\", \"On\""));
}

}  // namespace

int main() {
  TestCursorHider();
  TestWindowTitle();
  TestRenderResolution();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
