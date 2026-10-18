// Skip intro: default, persistence, reset, Gameplay row order, boot gate and the boot hook order. The gate and
// step search are pure; the hook is checked from source because the boot needs the running game.
// Run: hcu_skip_intro_tests

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "skip_intro.h"

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

// The text from the first begin marker up to the next end marker, or empty when either is missing.
std::string Between(const std::string& text, const char* begin, const char* end) {
  const std::size_t start = text.find(begin);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t stop = text.find(end, start);
  if (stop == std::string::npos) {
    return {};
  }
  return text.substr(start, stop - start);
}

void TestDefault() {
  std::printf("skip intro default\n");
  const std::string header = ReadSource("settings.h");
  CHECK(Has(header, "REXCVAR_DECLARE(bool, hcu_skip_intro);"));

  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(hcu_skip_intro, false,"));
}

void TestPersistence() {
  std::printf("skip intro persistence and reset\n");
  const std::string settings = ReadSource("settings.cpp");
  const std::string keys = Between(settings, "std::vector<std::string> GameSettingKeys() {", "  return keys;");
  CHECK(Has(keys, "\"hcu_skip_intro\","));
  CHECK(Has(settings, "for (const std::string& key : kGameSettingKeys) {"));
  CHECK(Has(settings, "ConfigValue(shipped, key).value_or(info ? info->default_value : std::string())"));
}

void TestGameplayRow() {
  std::printf("skip intro gameplay row\n");
  const std::string launcher = ReadSource("launcher.cpp");
  const std::string page = Between(launcher, "static void PageGameplay(trg::Ui& ui) {", "static void PageCheats");
  CHECK(!page.empty());

  const std::size_t skip = page.find("\"hcu_skip_intro\"");
  const std::size_t frame_rate = page.find("\"hcu_show_fps\"");
  CHECK(skip != std::string::npos);
  CHECK(frame_rate != std::string::npos);
  CHECK(skip < frame_rate);  // first option, before the FPS rows
  CHECK(Has(page,
            "ui.Toggle(\"Skip intro\", \"Start directly at the title screen\", \"hcu_skip_intro\", false, \"Off\", "
            "\"On\");"));
  CHECK(Has(page, "\"hcu_show_fps\", true, \"Off\", \"On\""));
  CHECK(Has(page, "\"hcu_show_frame_counter\", false, \"Hidden\", \"Shown\""));
}

void TestBootGate() {
  std::printf("skip intro boot gate\n");
  CHECK(hcu::SkipsBootIntro(true, 1));
  CHECK(!hcu::SkipsBootIntro(false, 1));
  CHECK(!hcu::SkipsBootIntro(true, 0));
  CHECK(!hcu::SkipsBootIntro(false, 0));
}

void TestBootStep() {
  std::printf("skip intro boot completion step\n");
  const std::vector<uint32_t> steps = {0, 7, 2, 4, 3, 11, 1, 2};
  CHECK(hcu::BootCompleteIndex(steps) == std::optional<uint32_t>(5));
  CHECK(!hcu::BootCompleteIndex({0, 7, 2}).has_value());
  CHECK(!hcu::BootCompleteIndex({}).has_value());
}

void TestBootHook() {
  std::printf("skip intro boot hook\n");
  const std::string hook = ReadSource("skip_intro.cpp");
  CHECK(Has(hook, "REX_EXTERN(__imp__sub_824E52C8);"));
  CHECK(Has(hook, "REX_HOOK_RAW(sub_824E52C8) {"));
  CHECK(Has(hook, "REXCVAR_GET(hcu_skip_intro)"));

  const std::size_t original = hook.find("__imp__sub_824E52C8(ctx, base);");
  const std::size_t gate = hook.find("hcu::SkipsBootIntro(");
  const std::size_t store = hook.find("hcu::StoreBE32(base, boot + kBootStepIndexOffset, *step);");
  CHECK(original != std::string::npos);
  CHECK(gate != std::string::npos);
  CHECK(store != std::string::npos);
  CHECK(original < gate && gate < store);  // the original init runs first, then the gate

  CHECK(!Has(hook, "base[kBootIntroFlag] ="));  // the global boot flag is never written
  CHECK(!Has(hook, "sub_824F"));  // the title is never constructed or entered from here
  CHECK(!Has(hook, "sub_824EC550"));  // the scene manager keeps the transition
}

}  // namespace

int main() {
  TestDefault();
  TestPersistence();
  TestGameplayRow();
  TestBootGate();
  TestBootStep();
  TestBootHook();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
