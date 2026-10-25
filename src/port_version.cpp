#include "port_version.h"

namespace hcu {

const std::string& PortVersion() {
  static const std::string version = std::to_string(HCU_VERSION_MAJOR) + "." + std::to_string(HCU_VERSION_MINOR) +
                                     "." + std::to_string(HCU_VERSION_PATCH);
  return version;
}

}  // namespace hcu
