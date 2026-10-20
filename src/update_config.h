#pragma once

#include <filesystem>

#include "updater/update_controller.h"

namespace hcu {

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir);

}  // namespace hcu
