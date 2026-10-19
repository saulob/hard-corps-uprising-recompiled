#include "log_filter.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

REXCVAR_DEFINE_BOOL(detailed_logging, true, "Hard Corps",
                    "Keep logs for normal sessions (crash logs are always preserved)")
    .allowed({"true", "false"});

namespace hcu {
namespace {

// Informational lines kept in Minimal mode: the first-line facts a failure report needs.
constexpr std::string_view kEssentialInfo[] = {
    "Hard Corps: Uprising port ",
    "User data:",
    "Player settings:",
    "Graphics card:",
    "Internal render resolution:",
    "DXGI adapter:",
    "Vulkan device:",
    "Vulkan instance API version",
    "SDL video driver:",
    "Windows crash reporting enabled for current log.",
    "Execution complete",
    "Window closing",
    "Title terminated",
};

// Forwards the messages Minimal mode keeps to the file sink. `spdlog` locks the wrapper and the
// target separately, so it stays safe once the game's threads log.
class MinimalLogSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  explicit MinimalLogSink(spdlog::sink_ptr target) : target_(std::move(target)) {}

  const spdlog::sink_ptr& target() const { return target_; }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    const std::string_view category(msg.logger_name.data(), msg.logger_name.size());
    const std::string_view message(msg.payload.data(), msg.payload.size());
    if (ShouldLogInMinimalMode(msg.level, category, message)) {
      target_->log(msg);
    }
  }

  void flush_() override { target_->flush(); }
  void set_pattern_(const std::string& pattern) override { target_->set_pattern(pattern); }
  void set_formatter_(std::unique_ptr<spdlog::formatter> formatter) override {
    target_->set_formatter(std::move(formatter));
  }

 private:
  spdlog::sink_ptr target_;
};

spdlog::sink_ptr g_filter;
spdlog::sink_ptr g_filtered_sink;

// The GPU plugin registers its category on first use; do it here so its logger takes the filter.
void RegisterBuiltInCategories() {
  (void)rex::log::core();
  (void)rex::log::cpu();
  (void)rex::log::apu();
  (void)rex::log::gpu();
  (void)rex::log::krnl();
  (void)rex::log::sys();
  (void)rex::log::fs();
}

spdlog::sink_ptr FindFileSink() {
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    for (const spdlog::sink_ptr& sink : entry.logger->sinks()) {
      if (dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(sink.get()) != nullptr) {
        return sink;
      }
    }
  }
  if (spdlog::logger* core = rex::GetLoggerRaw(rex::log::core()); core != nullptr) {
    for (const spdlog::sink_ptr& sink : core->sinks()) {
      if (dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(sink.get()) != nullptr) {
        return sink;
      }
    }
  }
  return {};
}

void ReplaceSink(const spdlog::sink_ptr& from, const spdlog::sink_ptr& to) {
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    for (spdlog::sink_ptr& sink : entry.logger->sinks()) {
      if (sink == from) {
        sink = to;
      }
    }
  }
}

void InstallMinimalFilter() {
  if (g_filter) {
    return;
  }
  RegisterBuiltInCategories();
  const spdlog::sink_ptr file = FindFileSink();
  if (!file) {
    return;
  }
  auto filter = std::make_shared<MinimalLogSink>(file);
  filter->set_level(file->level());
  g_filtered_sink = file;
  g_filter = filter;
  ReplaceSink(file, filter);
}

void RemoveMinimalFilter() {
  if (!g_filter) {
    return;
  }
  ReplaceSink(g_filter, g_filtered_sink);
  g_filter.reset();
  g_filtered_sink.reset();
}

}  // namespace

bool ShouldLogInMinimalMode(spdlog::level::level_enum level, std::string_view category,
                            std::string_view message) {
  if (level >= spdlog::level::warn) {
    return true;
  }
  if (level < spdlog::level::info) {
    return false;
  }
  if (category == "gpu" && message.starts_with("DXGI adapter:")) {
    return true;
  }
  for (const std::string_view prefix : kEssentialInfo) {
    if (message.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

bool DetailedLoggingEnabled() {
  return rex::cvar::GetFlagByName("detailed_logging") != "false";
}

void ApplyDetailedLogging() {
  if (DetailedLoggingEnabled()) {
    RemoveMinimalFilter();
  } else {
    InstallMinimalFilter();
  }
}

bool MinimalLogFilterInstalled() {
  return g_filter != nullptr;
}

void ReleaseMinimalLogFilter() {
  RemoveMinimalFilter();
}

spdlog::sink_ptr UnwrapLogSink(const spdlog::sink_ptr& sink) {
  if (const auto* filter = dynamic_cast<MinimalLogSink*>(sink.get()); filter != nullptr) {
    return filter->target();
  }
  return sink;
}

}  // namespace hcu
