#include "game_files.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "stfs.h"

namespace hcu {
namespace {

constexpr const char* kStagingFolder = "game-install-temp";
constexpr const char* kBackupFolder = "game-install-backup";
constexpr const char* kDlcWorkFolder = "dlc-install-temp";
constexpr const char* kDlcRemoveFolder = "dlc-remove-temp";
constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr int kRenameAttempts = 10;
constexpr size_t kContentHeaderBytes = 0x148;
constexpr size_t kHeaderDisplayNameOffset = 8;
constexpr size_t kHeaderDisplayNameChars = 128;
constexpr size_t kHeaderFileNameOffset = 264;
constexpr size_t kHeaderFileNameBytes = 42;
constexpr size_t kHeaderTitleOffset = 0x140;
constexpr uint32_t kHddDeviceId = 1;

uint32_t Crc32(std::istream& in) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  std::vector<char> chunk(1 << 20);
  while (in) {
    in.read(chunk.data(), std::streamsize(chunk.size()));
    for (std::streamsize i = 0; i < in.gcount(); ++i) {
      crc = table[(crc ^ uint8_t(chunk[size_t(i)])) & 0xFF] ^ (crc >> 8);
    }
  }
  return ~crc;
}

// The antivirus scanner can hold a freshly written folder for a moment.
bool RenameWithRetry(const std::filesystem::path& from, const std::filesystem::path& to) {
  for (int attempt = 0; attempt < kRenameAttempts; ++attempt) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  return false;
}

// An existing folder is only replaced when it is empty or already holds game files.
bool IsReplaceable(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return false;
  }
  return std::filesystem::is_empty(dir, ec) || std::filesystem::exists(dir / "default.xex", ec) ||
         std::filesystem::exists(dir / "DataXENON", ec);
}

class StagingGuard {
 public:
  explicit StagingGuard(std::filesystem::path dir) : dir_(std::move(dir)) {}
  ~StagingGuard() {
    if (armed_) {
      std::error_code ec;
      std::filesystem::remove_all(dir_, ec);
    }
  }
  void Release() { armed_ = false; }

 private:
  std::filesystem::path dir_;
  bool armed_ = true;
};

InstallResult Failure(InstallStatus status, std::string detail = {}) {
  InstallResult result;
  result.status = status;
  result.detail = std::move(detail);
  return result;
}

InstallStatus FromStfs(stfs::Status status) {
  switch (status) {
    case stfs::Status::kOpenFailed:
      return InstallStatus::kOpenFailed;
    case stfs::Status::kNotPackage:
      return InstallStatus::kNotPackage;
    case stfs::Status::kUnsupported:
      return InstallStatus::kUnsupportedPackage;
    case stfs::Status::kCorrupt:
    case stfs::Status::kUnsafePath:
      return InstallStatus::kDamagedPackage;
    case stfs::Status::kCancelled:
      return InstallStatus::kCancelled;
    case stfs::Status::kWriteFailed:
    case stfs::Status::kOk:
      break;
  }
  return InstallStatus::kWriteFailed;
}

using Bytes = std::vector<uint8_t>;

void PutBe16(Bytes& out, size_t at, uint16_t value) {
  out[at] = uint8_t(value >> 8);
  out[at + 1] = uint8_t(value);
}

void PutBe32(Bytes& out, size_t at, uint32_t value) {
  PutBe16(out, at, uint16_t(value >> 16));
  PutBe16(out, at + 2, uint16_t(value));
}

uint32_t ReadBe32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint32_t ReadLe32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

struct SupportedDlc {
  DlcCharacter character;
  const char* name;
  const char* content_id;  // from the package metadata
};
constexpr SupportedDlc kSupportedDlc[] = {
    {DlcCharacter::kHarleyDaniels, "Harley Daniels", "AA928CFD0AED7A13DC4BD96128CB4967C8B738A0"},
    {DlcCharacter::kSayuri, "Sayuri", "73168E136E602ACC9BEBAB434297CF803B03E305"},
    {DlcCharacter::kLeviathan, "Leviathan", "6695631AE573EDFFACD33A6D32B4D005156AEC1E"},
};

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

constexpr std::string_view kNameSuffixes[] = {"(additional character)", " additional character"};

// Lower case, trimmed, single-spaced, without one trailing "Additional Character" (bare or in parentheses).
std::string CanonicalDlcName(std::string_view name) {
  std::string out;
  for (const char c : name) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  const auto trim = [&out] {
    while (!out.empty() && out.back() == ' ') {
      out.pop_back();
    }
  };
  trim();
  for (const std::string_view suffix : kNameSuffixes) {
    if (out.size() > suffix.size() && out.compare(out.size() - suffix.size(), suffix.size(), suffix) == 0) {
      out.resize(out.size() - suffix.size());
      break;
    }
  }
  trim();
  return out;
}

bool IsContentId(std::string_view id) {
  return id.size() == 40 && std::all_of(id.begin(), id.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
         });
}

bool HasReparsePoint(const std::filesystem::path& path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  std::error_code ec;
  return std::filesystem::is_symlink(path, ec);
#endif
}

bool IsPlainFile(const std::filesystem::path& path) {
  std::error_code ec;
  return !HasReparsePoint(path) && std::filesystem::is_regular_file(path, ec);
}

// True when `child` is a real folder directly inside `parent`, reached without a link on the way.
bool IsPlainChildDirectory(const std::filesystem::path& parent, const std::filesystem::path& child) {
  std::error_code ec;
  const std::filesystem::path real_parent = std::filesystem::canonical(parent, ec);
  if (ec) {
    return false;
  }
  const std::filesystem::path real_child = std::filesystem::canonical(child, ec);
  if (ec) {
    return false;
  }
  return real_child == real_parent / child.filename() && !HasReparsePoint(child) &&
         std::filesystem::is_directory(child, ec);
}

// Recursive deletion must not follow a link, so any link or junction under `dir` makes the folder unsafe.
bool ContainsReparsePoint(const std::filesystem::path& dir) {
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (HasReparsePoint(it->path())) {
      return true;
    }
  }
  return static_cast<bool>(ec);
}

// Nothing at `path`, or a plain file. A link is never followed, so a dangling link is not treated as absent.
bool IsAbsentOrPlainFile(const std::filesystem::path& path) {
  if (HasReparsePoint(path)) {
    return false;
  }
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  return status.type() == std::filesystem::file_type::not_found || (!ec && std::filesystem::is_regular_file(status));
}

// Nothing at `path`, or a folder with no link at or under it. Only these may be deleted recursively.
bool IsAbsentOrPlainFolder(const std::filesystem::path& path) {
  if (HasReparsePoint(path)) {
    return false;
  }
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (status.type() == std::filesystem::file_type::not_found) {
    return true;
  }
  return !ec && std::filesystem::is_directory(status) && !ContainsReparsePoint(path);
}

// A DLC work folder is exactly <root>/dlc-install-temp or <root>/dlc-remove-temp and holds no link.
// Nothing else is ever cleared.
bool IsSafeWorkFolder(const std::filesystem::path& root, const std::filesystem::path& work) {
  return root.is_absolute() && (work == root / kDlcWorkFolder || work == root / kDlcRemoveFolder) &&
         IsAbsentOrPlainFolder(work);
}

// Clears the work folder when the operation ends, unless released. A folder that fails the check is left alone.
class WorkGuard {
 public:
  WorkGuard(std::filesystem::path root, std::filesystem::path work) : root_(std::move(root)), work_(std::move(work)) {}
  ~WorkGuard() {
    if (armed_ && IsSafeWorkFolder(root_, work_)) {
      std::error_code ec;
      std::filesystem::remove_all(work_, ec);
    }
  }
  void Release() { armed_ = false; }

 private:
  std::filesystem::path root_;
  std::filesystem::path work_;
  bool armed_ = true;
};

bool IsSameOrInside(const std::filesystem::path& parent, const std::filesystem::path& child) {
  const std::filesystem::path relative = child.lexically_relative(parent);
  return !relative.empty() && *relative.begin() != "..";
}

// The game folder must not be `folder` or inside it, or removing `folder` would delete the game.
bool GameFolderWouldBeRemoved(const std::filesystem::path& folder, const std::filesystem::path& game_dir) {
  std::error_code ec;
  const std::filesystem::path real_folder = std::filesystem::canonical(folder, ec);
  if (ec) {
    return true;
  }
  const std::filesystem::path real_game = std::filesystem::canonical(game_dir, ec);
  return !ec && IsSameOrInside(real_folder, real_game);
}

bool RemoveRegularFile(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::remove(path, ec) && !ec;
}

std::string ContentTypeFolder(uint32_t content_type) {
  char name[9];
  std::snprintf(name, sizeof(name), "%08X", content_type);
  return name;
}

// The SDK's content layout: <user data>/<xuid>/<title>/<content type>/<file name>, with marketplace content
// under the zero xuid. Its headers sit in a Headers folder beside the content folders.
std::filesystem::path DlcTitleFolder(const std::filesystem::path& user_data_root) {
  char title[9];
  std::snprintf(title, sizeof(title), "%08X", kTitleId);
  return user_data_root / "0000000000000000" / title;
}

std::filesystem::path DlcFolder(const std::filesystem::path& user_data_root) {
  return DlcTitleFolder(user_data_root) / ContentTypeFolder(kDlcContentType);
}

std::filesystem::path DlcHeaderPath(const std::filesystem::path& user_data_root, const std::string& content_id) {
  return DlcTitleFolder(user_data_root) / "Headers" / ContentTypeFolder(kDlcContentType) / (content_id + ".header");
}

std::string ToUtf8(const std::u16string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    uint32_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
      ++i;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      c = 0xFFFD;
    }
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

// The .header file the SDK reads for marketplace content: its content record, then the licence mask when there is one.
bool WriteContentHeader(const std::filesystem::path& path, const std::string& file_name,
                        const std::u16string& display_name, uint32_t license_mask) {
  Bytes header(kContentHeaderBytes, 0);
  PutBe32(header, 0, kHddDeviceId);
  PutBe32(header, 4, kDlcContentType);
  for (size_t i = 0; i < std::min(display_name.size(), kHeaderDisplayNameChars - 1); ++i) {
    PutBe16(header, kHeaderDisplayNameOffset + 2 * i, uint16_t(display_name[i]));
  }
  std::memcpy(&header[kHeaderFileNameOffset], file_name.data(), std::min(file_name.size(), kHeaderFileNameBytes));
  PutBe32(header, kHeaderTitleOffset, kTitleId);
  if (license_mask != 0) {
    for (int shift = 0; shift < 32; shift += 8) {
      header.push_back(uint8_t(license_mask >> shift));
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
  out.close();
  return !out.fail();
}

// Marks `info` verified only when the header names this title, the marketplace content type and `info.content_id`.
void ReadVerifiedHeader(const std::filesystem::path& path, DlcInfo& info) {
  std::error_code ec;
  if (!IsPlainFile(path) || std::filesystem::file_size(path, ec) < kContentHeaderBytes || ec) {
    return;
  }
  Bytes data(kContentHeaderBytes + 4, 0);
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
  const size_t read = size_t(in.gcount());
  const std::string file_name(reinterpret_cast<const char*>(&data[kHeaderFileNameOffset]), kHeaderFileNameBytes);
  if (ReadBe32(&data[4]) != kDlcContentType || ReadBe32(&data[kHeaderTitleOffset]) != kTitleId ||
      std::string(file_name.c_str()) != info.content_id) {
    return;
  }
  info.verified = true;

  std::u16string name;
  for (size_t i = 0; i < kHeaderDisplayNameChars; ++i) {
    const size_t at = kHeaderDisplayNameOffset + 2 * i;
    const char16_t c = char16_t(data[at] << 8 | data[at + 1]);
    if (c == 0) {
      break;
    }
    name.push_back(c);
  }
  if (!name.empty()) {
    info.display_name = ToUtf8(name);
  }
  info.character = ClassifyDlc(info.content_id, info.display_name);
  if (read >= kContentHeaderBytes + 4) {
    info.license_mask = ReadLe32(&data[kContentHeaderBytes]);
  }
}

uint64_t CountFiles(const std::filesystem::path& dir) {
  uint64_t count = 0;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      ++count;
    }
  }
  return count;
}

enum class SwapResult { kActivated, kUnchanged, kStranded };

// Moves `staged` into place and keeps the previous `live` in `backup`, so the caller can still revert.
// kStranded: the previous copy could not be put back and is still in `backup`.
SwapResult SwapIn(const std::filesystem::path& staged, const std::filesystem::path& live,
                  const std::filesystem::path& backup, bool& had_previous) {
  std::error_code ec;
  had_previous = std::filesystem::exists(live, ec);
  if (had_previous && !RenameWithRetry(live, backup)) {
    return SwapResult::kUnchanged;
  }
  std::filesystem::create_directories(live.parent_path(), ec);
  if (!RenameWithRetry(staged, live)) {
    return had_previous && !RenameWithRetry(backup, live) ? SwapResult::kStranded : SwapResult::kUnchanged;
  }
  return SwapResult::kActivated;
}

// Removes the activated folder, then restores the previous one. False when the previous copy is not back in place.
bool RevertSwap(const std::filesystem::path& live, const std::filesystem::path& backup, bool had_previous) {
  if (!IsAbsentOrPlainFolder(live)) {
    return false;
  }
  std::error_code ec;
  std::filesystem::remove_all(live, ec);
  if (ec || std::filesystem::exists(live, ec)) {
    return false;
  }
  return !had_previous || RenameWithRetry(backup, live);
}

}  // namespace

GameCheck CheckGameFiles(const std::filesystem::path& game_dir) {
  GameCheck check;
  const auto xex = game_dir / "default.xex";
  std::error_code ec;
  if (game_dir.empty() || !std::filesystem::is_regular_file(xex, ec) ||
      !std::filesystem::is_directory(game_dir / "DataXENON", ec)) {
    return check;
  }
  check.state = GameFiles::kUnsupported;
  if (std::filesystem::file_size(xex, ec) != kXexSize || ec) {
    return check;
  }
  std::ifstream in(xex, std::ios::binary);
  check.xex_crc32 = Crc32(in);
  if (check.xex_crc32 == kXexCrc32) {
    check.state = GameFiles::kReady;
  }
  return check;
}

std::string GameFilesLogText(const GameCheck& check) {
  const char* state = "missing";
  if (check.state == GameFiles::kReady) {
    state = "valid";
  } else if (check.state == GameFiles::kUnsupported) {
    state = "unsupported version";
  }
  std::string text = std::string("Game files: ") + state + " (default.xex";
  if (check.xex_crc32 != 0) {
    char crc[16];
    std::snprintf(crc, sizeof(crc), "%08X", check.xex_crc32);
    text += std::string(", size ") + std::to_string(kXexSize) + ", CRC32 " + crc;
  }
  return text + ")";
}

InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 const InstallHooks& hooks) {
  const auto phase = [&](const char* text) {
    if (hooks.phase) {
      hooks.phase(text);
    }
  };

  std::error_code ec;
  std::filesystem::path target = std::filesystem::absolute(game_dir, ec).lexically_normal();
  if (!target.has_filename()) {
    target = target.parent_path();
  }
  const std::filesystem::path parent = target.parent_path();
  if (game_dir.empty() || ec || parent.empty() || parent == target) {
    return Failure(InstallStatus::kNoGameFolder);
  }

  phase("Reading package...");
  stfs::Package pkg;
  stfs::Result read = pkg.Open(package);
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  if (pkg.title_id() != kTitleId) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "title ID %08X", pkg.title_id());
    InstallResult result = Failure(InstallStatus::kWrongTitle, detail);
    result.title_id = pkg.title_id();
    return result;
  }
  if (pkg.content_type() != kGameContentType) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "content type %08X", pkg.content_type());
    return Failure(InstallStatus::kNotGamePackage, detail);
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }

  std::filesystem::create_directories(parent, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + parent.string() + ": " + ec.message());
  }
  if (std::filesystem::exists(target, ec) && !IsReplaceable(target)) {
    return Failure(InstallStatus::kForeignFolder, target.string());
  }

  const std::filesystem::path staging = parent / kStagingFolder;
  const std::filesystem::path backup = parent / kBackupFolder;
  std::filesystem::remove_all(staging, ec);
  if (ec || std::filesystem::exists(staging, ec)) {
    return Failure(InstallStatus::kWriteFailed, "cannot clear " + staging.string());
  }
  const auto space = std::filesystem::space(parent, ec);
  if (!ec && space.available < pkg.total_bytes() + kSpaceMargin) {
    return Failure(InstallStatus::kNoSpace, "needs " + std::to_string(pkg.total_bytes()) + " bytes, " +
                                                std::to_string(space.available) + " available");
  }
  std::filesystem::create_directories(staging, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + staging.string() + ": " + ec.message());
  }
  StagingGuard guard(staging);

  phase("Extracting game files...");
  const stfs::Result extracted = pkg.ExtractTo(staging, hooks.progress);
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  phase("Verifying installation...");
  switch (CheckGameFiles(staging).state) {
    case GameFiles::kReady:
      break;
    case GameFiles::kUnsupported:
      return Failure(InstallStatus::kWrongVersion);
    case GameFiles::kMissing:
      return Failure(InstallStatus::kIncomplete);
  }
  if (hooks.progress && !hooks.progress(pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(InstallStatus::kCancelled);
  }

  phase("Finishing...");
  const bool replacing = std::filesystem::exists(target, ec);
  if (replacing) {
    std::filesystem::remove_all(backup, ec);
    if (!RenameWithRetry(target, backup)) {
      return Failure(InstallStatus::kWriteFailed, "cannot move aside " + target.string());
    }
  }
  if (!RenameWithRetry(staging, target)) {
    if (replacing) {
      RenameWithRetry(backup, target);
    }
    return Failure(InstallStatus::kWriteFailed, "cannot activate " + target.string());
  }
  guard.Release();
  if (replacing) {
    std::filesystem::remove_all(backup, ec);
  }

  InstallResult result;
  result.bytes = pkg.total_bytes();
  result.files = pkg.file_count();
  result.title_id = pkg.title_id();
  return result;
}

InstallResult InstallDlcFromPackage(const std::filesystem::path& package, const std::filesystem::path& user_data_root,
                                    const InstallHooks& hooks, DlcInfo* installed) {
  const auto phase = [&](const char* text) {
    if (hooks.phase) {
      hooks.phase(text);
    }
  };
  if (user_data_root.empty()) {
    return Failure(InstallStatus::kNoUserFolder);
  }
  std::error_code ec;
  const std::filesystem::path root = std::filesystem::absolute(user_data_root, ec).lexically_normal();
  if (ec) {
    return Failure(InstallStatus::kNoUserFolder, ec.message());
  }

  phase("Reading package...");
  stfs::Package pkg;
  stfs::Result read = pkg.Open(package);
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  if (pkg.title_id() != kTitleId) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "title ID %08X", pkg.title_id());
    InstallResult result = Failure(InstallStatus::kWrongTitle, detail);
    result.title_id = pkg.title_id();
    return result;
  }
  if (pkg.content_type() != kDlcContentType) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "content type %08X", pkg.content_type());
    return Failure(InstallStatus::kNotDlcPackage, detail);
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  const std::string content_id = pkg.content_id_hex();
  if (content_id == std::string(content_id.size(), '0')) {
    return Failure(InstallStatus::kDamagedPackage, "no content ID");
  }
  if (pkg.file_count() == 0) {
    return Failure(InstallStatus::kDamagedPackage, "no files");
  }

  std::filesystem::create_directories(root, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + root.string() + ": " + ec.message());
  }
  const std::filesystem::path work = root / kDlcWorkFolder;
  if (!IsSafeWorkFolder(root, work)) {
    return Failure(InstallStatus::kUnsafeTarget, "unsafe DLC work folder " + work.string());
  }
  std::filesystem::remove_all(work, ec);
  if (ec || std::filesystem::exists(work, ec)) {
    return Failure(InstallStatus::kWriteFailed, "cannot clear " + work.string());
  }
  const auto space = std::filesystem::space(root, ec);
  if (!ec && space.available < pkg.total_bytes() + kSpaceMargin) {
    return Failure(InstallStatus::kNoSpace, "needs " + std::to_string(pkg.total_bytes()) + " bytes, " +
                                                std::to_string(space.available) + " available");
  }
  WorkGuard guard(root, work);
  const std::filesystem::path staged = work / "staged";
  const std::filesystem::path backup = work / "backup";
  const std::filesystem::path files = staged / "files";
  std::filesystem::create_directories(files, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + files.string() + ": " + ec.message());
  }
  std::filesystem::create_directories(backup, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + backup.string() + ": " + ec.message());
  }

  phase("Extracting DLC...");
  const stfs::Result extracted = pkg.ExtractTo(files, hooks.progress);
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  phase("Verifying installation...");
  if (CountFiles(files) != pkg.file_count()) {
    return Failure(InstallStatus::kWriteFailed, "extracted files do not match the package");
  }
  const std::u16string display_name =
      pkg.display_name().empty() ? std::u16string(content_id.begin(), content_id.end()) : pkg.display_name();
  const std::filesystem::path staged_header = staged / "header.bin";
  if (!WriteContentHeader(staged_header, content_id, display_name, pkg.license_mask())) {
    return Failure(InstallStatus::kWriteFailed, "cannot write " + staged_header.string());
  }
  if (hooks.progress && !hooks.progress(pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(InstallStatus::kCancelled);
  }

  phase("Finishing...");
  const std::filesystem::path folder = DlcFolder(root) / content_id;
  const std::filesystem::path header = DlcHeaderPath(root, content_id);
  if (!IsSafeWorkFolder(root, work)) {
    return Failure(InstallStatus::kUnsafeTarget, "unsafe DLC work folder " + work.string());
  }
  if (!IsAbsentOrPlainFolder(folder) || !IsAbsentOrPlainFile(header)) {
    return Failure(InstallStatus::kUnsafeTarget, "unsafe installed DLC " + folder.string());
  }
  // A previous copy that could not be put back stays in the work folder, so that folder is kept.
  const auto activation_failed = [&](const std::filesystem::path& target, bool keep_work_folder) {
    if (keep_work_folder) {
      guard.Release();
    }
    std::string detail = "cannot activate " + target.string();
    if (keep_work_folder) {
      detail += "; previous copy kept in " + backup.string();
    }
    return Failure(InstallStatus::kWriteFailed, std::move(detail));
  };
  bool folder_had_previous = false;
  const SwapResult folder_swap = SwapIn(files, folder, backup / "folder", folder_had_previous);
  if (folder_swap != SwapResult::kActivated) {
    return activation_failed(folder, folder_swap == SwapResult::kStranded);
  }
  bool header_had_previous = false;
  const SwapResult header_swap = SwapIn(staged_header, header, backup / "header", header_had_previous);
  if (header_swap != SwapResult::kActivated) {
    const bool reverted = RevertSwap(folder, backup / "folder", folder_had_previous);
    return activation_failed(header, !reverted || header_swap == SwapResult::kStranded);
  }

  if (installed) {
    installed->content_id = content_id;
    installed->display_name = ToUtf8(display_name);
    installed->license_mask = pkg.license_mask();
    installed->character = ClassifyDlc(content_id, installed->display_name);
  }
  InstallResult result;
  result.bytes = pkg.total_bytes();
  result.files = pkg.file_count();
  result.title_id = pkg.title_id();
  return result;
}

std::vector<DlcInfo> ListInstalledDlc(const std::filesystem::path& user_data_root) {
  std::vector<DlcInfo> installed;
  if (user_data_root.empty()) {
    return installed;
  }
  std::error_code ec;
  for (std::filesystem::directory_iterator it(DlcFolder(user_data_root), ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!it->is_directory(ec)) {
      continue;
    }
    DlcInfo info;
    info.content_id = it->path().filename().string();
    info.display_name = info.content_id;
    ReadVerifiedHeader(DlcHeaderPath(user_data_root, info.content_id), info);
    installed.push_back(std::move(info));
  }
  std::sort(installed.begin(), installed.end(),
            [](const DlcInfo& a, const DlcInfo& b) { return a.content_id < b.content_id; });
  return installed;
}

DlcCharacter ClassifyDlc(std::string_view content_id, std::string_view display_name) {
  for (const SupportedDlc& entry : kSupportedDlc) {
    if (EqualsIgnoreCase(content_id, entry.content_id)) {
      return entry.character;
    }
  }
  const std::string name = CanonicalDlcName(display_name);
  for (const SupportedDlc& entry : kSupportedDlc) {
    if (EqualsIgnoreCase(name, entry.name)) {
      return entry.character;
    }
  }
  return DlcCharacter::kUnknown;
}

const char* DlcCharacterName(DlcCharacter character) {
  for (const SupportedDlc& entry : kSupportedDlc) {
    if (entry.character == character) {
      return entry.name;
    }
  }
  return "";
}

RemoveResult RemoveInstalledDlc(const std::filesystem::path& user_data_root, const std::filesystem::path& game_dir,
                                const std::string& content_id) {
  if (!IsContentId(content_id)) {
    return {RemoveStatus::kUnsafeTarget, "malformed content ID"};
  }
  if (user_data_root.empty()) {
    return {RemoveStatus::kNotInstalled};
  }
  std::error_code ec;
  const std::filesystem::path root = std::filesystem::absolute(user_data_root, ec).lexically_normal();
  if (ec) {
    return {RemoveStatus::kUnsafeTarget, ec.message()};
  }
  const std::vector<DlcInfo> installed = ListInstalledDlc(root);
  const auto entry = std::find_if(installed.begin(), installed.end(),
                                  [&](const DlcInfo& info) { return info.content_id == content_id; });
  if (entry == installed.end()) {
    return {RemoveStatus::kNotInstalled};
  }
  if (!entry->verified) {
    return {RemoveStatus::kUnverified, content_id};
  }

  const std::filesystem::path dlc_root = DlcFolder(root);
  const std::filesystem::path folder = dlc_root / content_id;
  const std::filesystem::path header = DlcHeaderPath(root, content_id);
  if (!IsPlainChildDirectory(dlc_root, folder) || !IsPlainFile(header) || ContainsReparsePoint(folder) ||
      GameFolderWouldBeRemoved(folder, game_dir)) {
    return {RemoveStatus::kUnsafeTarget, content_id};
  }

  const std::filesystem::path work = root / kDlcRemoveFolder;
  if (!IsSafeWorkFolder(root, work)) {
    return {RemoveStatus::kUnsafeTarget, "unsafe removal folder " + work.string()};
  }
  std::filesystem::remove_all(work, ec);
  if (ec || std::filesystem::exists(work, ec)) {
    return {RemoveStatus::kWriteFailed, "cannot clear " + work.string()};
  }
  std::filesystem::create_directories(work, ec);
  if (ec) {
    return {RemoveStatus::kWriteFailed, "cannot create " + work.string() + ": " + ec.message()};
  }
  WorkGuard guard(root, work);
  const std::filesystem::path moved = work / "folder";
  if (!RenameWithRetry(folder, moved)) {
    return {RemoveStatus::kWriteFailed, "cannot move " + folder.string()};
  }
  if (!RemoveRegularFile(header)) {
    if (!RenameWithRetry(moved, folder)) {
      guard.Release();
      return {RemoveStatus::kWriteFailed, "cannot restore " + folder.string() + " from " + moved.string()};
    }
    return {RemoveStatus::kWriteFailed, "cannot remove " + header.string()};
  }

  for (const DlcInfo& left : ListInstalledDlc(root)) {
    if (left.content_id == content_id) {
      return {RemoveStatus::kWriteFailed, "still installed: " + content_id};
    }
  }
  return {RemoveStatus::kRemoved};
}

RemoveMessage DescribeRemove(RemoveStatus status) {
  switch (status) {
    case RemoveStatus::kRemoved:
      return {"DLC removed", "You can install the package again later."};
    case RemoveStatus::kNotInstalled:
      return {"Not installed", "This DLC is not installed."};
    case RemoveStatus::kUnverified:
      return {"Cannot remove this DLC", "The installed package could not be verified, so it was left in place."};
    case RemoveStatus::kUnsafeTarget:
      return {"Cannot remove this DLC", "Its folder is not a plain folder in the DLC storage, so nothing was removed."};
    case RemoveStatus::kWriteFailed:
      return {"Could not remove the DLC",
              "The DLC could not be removed. Close any program using its files and try again."};
  }
  return {"Could not remove the DLC", ""};
}

InstallMessage DescribeInstall(InstallStatus status) {
  switch (status) {
    case InstallStatus::kInstalled:
      return {"Game installed", "Press Play to start the game."};
    case InstallStatus::kCancelled:
      return {"Install cancelled", "The game files were not changed."};
    case InstallStatus::kNoGameFolder:
      return {"No game folder", "Start the game with --game_data_root to choose where the game files go."};
    case InstallStatus::kForeignFolder:
      return {"Game folder not empty", "The game folder holds other files, so it was not replaced."};
    case InstallStatus::kOpenFailed:
      return {"Could not open the file", "Check that the file exists and that it is not in use."};
    case InstallStatus::kNotPackage:
      return {"Not an Xbox 360 package", "The selected file is not an Xbox 360 content package."};
    case InstallStatus::kWrongTitle:
      return {"Wrong package", "This is not a Hard Corps: Uprising package."};
    case InstallStatus::kUnsupportedPackage:
      return {"Unsupported package", "This package uses a layout the installer cannot read."};
    case InstallStatus::kDamagedPackage:
      return {"Damaged package", "The package is damaged or incomplete. Check the file and try again."};
    case InstallStatus::kNoSpace:
      return {"Not enough disk space", "Free up space next to the game files and try again."};
    case InstallStatus::kWriteFailed:
      return {"Could not write the game files", "Check that the game folder is writable and not in use."};
    case InstallStatus::kWrongVersion:
      return {"Different version of the game",
              "This port requires the supported Hard Corps: Uprising Xbox Live Arcade release."};
    case InstallStatus::kIncomplete:
      return {"Game files missing", "The package does not contain the game files this port needs."};
    case InstallStatus::kNotGamePackage:
      return {"Not the game package",
              "This package is DLC or another kind of package. Install the game from its Xbox Live Arcade package."};
    case InstallStatus::kNotDlcPackage:
      return {"Not a DLC package", "The selected file is not Xbox 360 Marketplace content (content type 00000002)."};
    case InstallStatus::kNoUserFolder:
      return {"No user data folder", "Start the game with --user_data_root to choose where DLC is stored."};
    case InstallStatus::kUnsafeTarget:
      return {"Could not prepare the DLC install folder",
              "Nothing was changed. Installed DLC and the game files were left as they are."};
  }
  return {"Install failed", ""};
}

}  // namespace hcu
