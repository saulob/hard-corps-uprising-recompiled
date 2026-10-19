#include "session_log.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <rex/logging.h>

#include "active_log.h"
#include "log_filter.h"

namespace hcu {
namespace {

bool LogContainsCrashReport(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return false;
  }
  constexpr size_t kNeedle = sizeof("CRASH REPORT") - 1;
  std::string window;
  char buffer[4096];
  while (in) {
    in.read(buffer, sizeof(buffer));
    window.append(buffer, static_cast<size_t>(in.gcount()));
    if (window.find("CRASH REPORT") != std::string::npos) {
      return true;
    }
    if (window.size() > kNeedle) {
      window.erase(0, window.size() - kNeedle);
    }
  }
  return false;
}

}  // namespace

void InstallSessionLogCleanup() {
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  std::atexit(FinalizeSessionLog);
}

void FinalizeSessionLog() {
  if (DetailedLoggingEnabled()) {
    return;
  }
  const std::filesystem::path file = ActiveLogFile();
  if (file.empty()) {
    return;
  }

  rex::FlushLogging();
  std::error_code ec;
  if (!std::filesystem::exists(file, ec) || ec) {
    return;
  }
  if (LogContainsCrashReport(file)) {
    return;
  }

  ReleaseMinimalLogFilter();
  rex::ShutdownLogging();
  std::filesystem::remove(file, ec);
}

}  // namespace hcu
