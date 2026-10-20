#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hcu {

inline constexpr uint32_t kTitleId = 0x58410A18;
// Content types of the packages this port accepts: the Xbox Live Arcade game and Xbox 360 Marketplace content.
inline constexpr uint32_t kGameContentType = 0x000D0000;
inline constexpr uint32_t kDlcContentType = 0x00000002;

// The default.xex the code was recompiled from (XBLA game version 0.0.0.3, see tools/codegen.ps1).
inline constexpr uint64_t kXexSize = 12955648;
inline constexpr uint32_t kXexCrc32 = 0x3DF903B9;

enum class GameFiles { kReady, kMissing, kUnsupported };

struct GameCheck {
  GameFiles state = GameFiles::kMissing;
  uint32_t xex_crc32 = 0;  // set when default.xex was read
};

// The one readiness check: launcher state, install verification and the clean-start decision all use it.
GameCheck CheckGameFiles(const std::filesystem::path& game_dir);

// A support log line about the game files that carries no folder: name, size, CRC32 and state.
std::string GameFilesLogText(const GameCheck& check);

enum class InstallStatus {
  kInstalled,
  kCancelled,
  kNoGameFolder,
  kForeignFolder,
  kOpenFailed,
  kNotPackage,
  kWrongTitle,
  kUnsupportedPackage,
  kDamagedPackage,
  kNoSpace,
  kWriteFailed,
  kWrongVersion,
  kIncomplete,
  kNotGamePackage,
  kNotDlcPackage,
  kNoUserFolder,
  kUnsafeTarget,
};

struct InstallResult {
  InstallStatus status = InstallStatus::kInstalled;
  std::string detail;  // for the log, not the UI
  uint32_t title_id = 0;
  uint64_t bytes = 0;
  size_t files = 0;
  bool ok() const { return status == InstallStatus::kInstalled; }
};

struct InstallHooks {
  std::function<void(const char* phase)> phase;
  // Returns false to cancel. Cancelling is possible until the new files are activated.
  std::function<bool(uint64_t done, uint64_t total)> progress;
};

// Extracts the package into a staging folder next to `game_dir`, checks it with CheckGameFiles and only
// then swaps it in. On any failure or cancellation `game_dir` is left as it was.
InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 const InstallHooks& hooks = {});

struct InstallMessage {
  const char* title;
  const char* text;
};
InstallMessage DescribeInstall(InstallStatus status);

enum class DlcCharacter { kUnknown, kHarleyDaniels, kSayuri, kLeviathan };
inline constexpr DlcCharacter kDlcCharacters[] = {DlcCharacter::kHarleyDaniels, DlcCharacter::kSayuri,
                                                  DlcCharacter::kLeviathan};

// The character a package identifies: by its content ID when that is a known one, else by its English display name
// ("Sayuri", "Sayuri Additional Character" and "Sayuri(Additional Character)" are accepted). kUnknown otherwise.
// The caller has already checked the title ID and content type.
DlcCharacter ClassifyDlc(std::string_view content_id, std::string_view display_name);
const char* DlcCharacterName(DlcCharacter character);

// A DLC package installed under the user data folder, where the SDK's content manager finds it.
struct DlcInfo {
  std::string content_id;    // 40 hex digits: the package's content ID and its folder name
  std::string display_name;  // UTF-8, from the package metadata; the content ID when it has none
  uint32_t license_mask = 0;
  DlcCharacter character = DlcCharacter::kUnknown;
  bool verified = false;     // the header names this title, content type and content ID; only then can it be removed
};

// Installs a DLC package for this title as marketplace content under `user_data_root`, keyed by its content ID.
// Other DLC, the game folder and the user's saves are not touched. A failed or cancelled install leaves the installed
// copy as it was; one that cannot be put back is kept in the work folder and named in `detail`.
InstallResult InstallDlcFromPackage(const std::filesystem::path& package, const std::filesystem::path& user_data_root,
                                    const InstallHooks& hooks = {}, DlcInfo* installed = nullptr);

std::vector<DlcInfo> ListInstalledDlc(const std::filesystem::path& user_data_root);

enum class RemoveStatus { kRemoved, kNotInstalled, kUnverified, kUnsafeTarget, kWriteFailed };

struct RemoveResult {
  RemoveStatus status = RemoveStatus::kRemoved;
  std::string detail;  // for the log, not the UI
  bool ok() const { return status == RemoveStatus::kRemoved; }
};

struct RemoveMessage {
  const char* title;
  const char* text;
};
RemoveMessage DescribeRemove(RemoveStatus status);

// Removes the content folder and the header of one installed DLC package and nothing else. Only packages that
// ListInstalledDlc reports as verified are removed. `game_dir` must not be, or be inside, the removed folder.
RemoveResult RemoveInstalledDlc(const std::filesystem::path& user_data_root, const std::filesystem::path& game_dir,
                                const std::string& content_id);

}  // namespace hcu
