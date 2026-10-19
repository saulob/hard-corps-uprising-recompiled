#include "session_log.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <rex/logging.h>

#include "active_log.h"
#include "log_filter.h"

namespace hcu {
namespace {

#if defined(_WIN32)
constexpr wchar_t kSessionLogVariable[] = L"HCU_SESSION_LOG";
constexpr wchar_t kSessionParentVariable[] = L"HCU_SESSION_PARENT";
#else
constexpr char kSessionLogVariable[] = "HCU_SESSION_LOG";
constexpr char kSessionParentVariable[] = "HCU_SESSION_PARENT";
#endif

std::atomic<bool> g_handed_off{false};

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

void WaitForPreviousProcess(unsigned long pid) {
#if defined(_WIN32)
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
  if (process == nullptr) {
    return;
  }
  WaitForSingleObject(process, 30000);
  CloseHandle(process);
#else
  if (pid == 0) {
    return;
  }
  for (int attempt = 0; attempt < 3000; ++attempt) {
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
#endif
}

void ClearInheritedSession() {
#if defined(_WIN32)
  SetEnvironmentVariableW(kSessionLogVariable, nullptr);
  SetEnvironmentVariableW(kSessionParentVariable, nullptr);
#else
  unsetenv(kSessionLogVariable);
  unsetenv(kSessionParentVariable);
#endif
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
  if (g_handed_off.load(std::memory_order_acquire)) {
    return;
  }
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

void BeginSessionLogHandoff() {
  const std::filesystem::path file = ActiveLogFile();
  if (file.empty()) {
    return;
  }

  rex::FlushLogging();
  g_handed_off.store(true, std::memory_order_release);
#if defined(_WIN32)
  const std::wstring path = file.wstring();
  const std::wstring parent = std::to_wstring(GetCurrentProcessId());
  SetEnvironmentVariableW(kSessionLogVariable, path.c_str());
  SetEnvironmentVariableW(kSessionParentVariable, parent.c_str());
#else
  const std::string path = file.string();
  const std::string parent = std::to_string(getpid());
  setenv(kSessionLogVariable, path.c_str(), 1);
  setenv(kSessionParentVariable, parent.c_str(), 1);
#endif
}

void CancelSessionLogHandoff() {
  g_handed_off.store(false, std::memory_order_release);
  ClearInheritedSession();
}

std::optional<std::filesystem::path> TakeInheritedSessionLog() {
  std::filesystem::path file;
  unsigned long parent = 0;

#if defined(_WIN32)
  wchar_t path[32768];
  const DWORD path_length = GetEnvironmentVariableW(kSessionLogVariable, path, 32768);
  wchar_t pid[32];
  const DWORD pid_length = GetEnvironmentVariableW(kSessionParentVariable, pid, 32);
  ClearInheritedSession();
  if (path_length == 0 || path_length >= 32768) {
    return std::nullopt;
  }
  file = std::filesystem::path(std::wstring(path, path_length));
  if (pid_length > 0 && pid_length < 32) {
    parent = std::wcstoul(std::wstring(pid, pid_length).c_str(), nullptr, 10);
  }
#else
  const char* path = std::getenv(kSessionLogVariable);
  const char* pid = std::getenv(kSessionParentVariable);
  if (path == nullptr || *path == '\0') {
    ClearInheritedSession();
    return std::nullopt;
  }
  file = std::filesystem::path(path);
  if (pid != nullptr) {
    parent = std::strtoul(pid, nullptr, 10);
  }
  ClearInheritedSession();
#endif

  WaitForPreviousProcess(parent);
  return file;
}

}  // namespace hcu
