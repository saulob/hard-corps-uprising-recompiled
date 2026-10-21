#pragma once

#include <filesystem>

namespace hcu {

// The log file this run is writing: the configured `log_file`, or the target of the logger's file
// sink. Empty when file logging is not active.
std::filesystem::path ActiveLogFile();

}  // namespace hcu
