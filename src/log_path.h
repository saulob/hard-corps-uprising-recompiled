#pragma once

#include <filesystem>

namespace hcu {

// The folder holding the logs users send: the active log's folder, or <port folder>/logs with the
// AppImage fallback to <user data>/logs.
std::filesystem::path LogsFolder(const std::filesystem::path& port_folder,
                                 const std::filesystem::path& user_data_root,
                                 const std::filesystem::path& active_log, bool appimage);

}  // namespace hcu
