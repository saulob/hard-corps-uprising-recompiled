#include "log_path.h"

#include <system_error>

namespace hcu {

std::filesystem::path LogsFolder(const std::filesystem::path& port_folder,
                                 const std::filesystem::path& user_data_root,
                                 const std::filesystem::path& active_log, bool appimage) {
  if (!active_log.empty()) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(active_log, ec);
    return (ec ? active_log : absolute).parent_path();
  }
  const std::filesystem::path portable = port_folder / "logs";
  if (!appimage) {
    return portable;
  }
  std::error_code ec;
  std::filesystem::create_directories(portable, ec);
  if (std::filesystem::is_directory(portable, ec)) {
    return portable;
  }
  return user_data_root / "logs";
}

}  // namespace hcu
