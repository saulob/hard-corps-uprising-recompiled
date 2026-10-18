// Window-close lifecycle policy: a host window close hard-exits without runtime teardown, while a normal
// guest exit keeps the graceful teardown in OnShutdown. The decision is read from the app source (the
// window calls need the running game).
// Run: hcu_lifecycle_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

// The text of the member function that starts at `declaration`, up to its column-2 closing brace.
std::string FunctionBody(const std::string& source, const char* declaration) {
  const size_t start = source.find(declaration);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestHostWindowCloseHardExits() {
  std::printf("host window close hard-exits without runtime teardown\n");
  const std::string app = ReadSource("hard_corps_uprising_app.h");
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent&) override"));
  const std::string body = FunctionBody(app, "void OnClosing(");
  CHECK(Has(body, "rex::FlushLogging();"));
  CHECK(Has(body, "std::_Exit(0);"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "StopTitleThreads"));
  CHECK(!Has(body, "Shutdown("));
  CHECK(!Has(body, "DetachWindowHost"));
}

void TestNormalExitKeepsTeardown() {
  std::printf("normal guest exit keeps the graceful teardown\n");
  const std::string app = ReadSource("hard_corps_uprising_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  const std::string body = FunctionBody(app, "void OnShutdown(");
  CHECK(Has(body, "input->Shutdown();"));
  CHECK(Has(body, "DetachWindowHost();"));
}

}  // namespace

int main() {
  TestHostWindowCloseHardExits();
  TestNormalExitKeepsTeardown();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
