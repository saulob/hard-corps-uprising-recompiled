#include "update_config.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/logging.h>

#include "platform.h"
#include "port_version.h"
#include "updater/update_platform.h"

namespace hcu {
namespace {

constexpr const char* kReleasesUrl =
    "https://api.github.com/repos/saulob/hard-corps-uprising-recompiled/releases/latest";

std::optional<std::string> Environment(const char* name) {
#if defined(_WIN32)
  char* value = nullptr;
  size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || !value) {
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  if (result.empty()) {
    return std::nullopt;
  }
  return result;
#else
  const char* value = std::getenv(name);
  if (!value || !*value) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

bool TestMode() {
  const std::optional<std::string> value = Environment("HCU_UPDATE_TEST");
  return value && *value == "1";
}

std::optional<std::string> TestVariable(const char* name) {
  if (!TestMode()) {
    return std::nullopt;
  }
  return Environment(name);
}

std::vector<std::string> RestartArguments() {
  std::vector<std::string> arguments = trg::update::CurrentProcessArguments();
  arguments.erase(std::remove_if(arguments.begin(), arguments.end(),
                                 [](const std::string& argument) {
                                   return argument.rfind("--hcu_skip_launcher", 0) == 0;
                                 }),
                  arguments.end());
  arguments.emplace_back("--hcu_launcher=true");
  return arguments;
}

}  // namespace

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir) {
  using trg::update::SemanticVersion;
  using trg::update::UpdatePackageKind;

  trg::update::UpdateConfig config;
  config.current_version = PortVersion();
  config.releases_url = kReleasesUrl;
  config.user_agent = "HardCorpsUprising-Updater/" + PortVersion();
  config.install_dir = install_dir;
  config.restart_arguments = RestartArguments();

#if defined(_WIN32)
  config.package_kind = UpdatePackageKind::kZip;
  config.asset_name = [](const SemanticVersion& version) {
    return "HardCorpsUprising-v" + version.CoreString() + "-windows-x64.zip";
  };
  config.checksum_asset_name = [](const SemanticVersion& version) {
    return "HardCorpsUprising-v" + version.CoreString() + "-windows-x64.sha256";
  };
  config.restart_executable = install_dir / "hard_corps_uprising.exe";
  config.required_staged_files = {"hard_corps_uprising.exe"};
  config.install_supported = std::filesystem::exists(install_dir / "trg-updater.exe");
  if (!config.install_supported) {
    config.install_unsupported_reason =
        "The update helper is missing from this installation. Reinstall this release to enable automatic updates.";
  }
#else
  config.package_kind = UpdatePackageKind::kAppImage;
  config.asset_name = [](const SemanticVersion& version) {
    return "HardCorpsUprisingRecompiled-v" + version.CoreString() + "-x86_64.AppImage";
  };
  config.checksum_asset_name = [](const SemanticVersion& version) {
    return "HardCorpsUprisingRecompiled-v" + version.CoreString() + "-x86_64.sha256";
  };
  std::filesystem::path appimage;
  if (trg::update::HasOriginalAppImage(appimage)) {
    config.appimage_path = appimage;
    config.restart_executable = appimage;
    config.install_supported = true;
  } else {
    config.install_unsupported_reason = "Automatic installation is available in the AppImage build.";
  }
#endif

  config.log = [](std::string_view text) { REXLOG_INFO("Updater: {}", text); };

  if (const std::optional<std::string> version = TestVariable("HCU_UPDATE_TEST_CURRENT_VERSION"); version) {
    config.current_version = *version;
    REXLOG_WARN("Updater test mode: reporting version {}", config.current_version);
  }
  if (const std::optional<std::string> json = TestVariable("HCU_UPDATE_TEST_RELEASE_JSON"); json) {
    config.release_json_override = PathFromUtf8(*json);
  }
  if (const std::optional<std::string> hash = TestVariable("HCU_UPDATE_TEST_ASSET_SHA256"); hash) {
    config.sha256_override = *hash;
  }
  return config;
}

}  // namespace hcu
