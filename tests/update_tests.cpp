// Updater tests: semantic versions, release parsing, SHA-256, ZIP path safety, staging validation and rollback.
// Run: hcu_update_tests

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "updater/github_release.h"
#include "updater/install_transaction.h"
#include "updater/sha256.h"
#include "updater/third_party/miniz/miniz.h"
#include "updater/update_job.h"
#include "updater/update_paths.h"
#include "updater/update_zip.h"
#include "updater/version.h"

using namespace trg::update;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

class TempDir {
 public:
  TempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("hcu-update-tests-" + std::to_string(now));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), std::streamsize(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
  mz_zip_archive archive{};
  if (mz_zip_writer_init_heap(&archive, 0, 0) == MZ_FALSE) {
    return {};
  }
  for (const auto& [name, content] : entries) {
    if (mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), MZ_BEST_COMPRESSION) ==
        MZ_FALSE) {
      mz_zip_writer_end(&archive);
      return {};
    }
  }
  void* data = nullptr;
  size_t size = 0;
  if (mz_zip_writer_finalize_heap_archive(&archive, &data, &size) == MZ_FALSE) {
    mz_zip_writer_end(&archive);
    return {};
  }
  std::string result(static_cast<const char*>(data), size);
  mz_free(data);
  mz_zip_writer_end(&archive);
  return result;
}

void TestVersions() {
  std::printf("semantic versions\n");
  const auto v061 = ParseVersion("v0.6.1");
  const auto v061_plain = ParseVersion("0.6.1");
  const auto v0610 = ParseVersion("0.6.10");
  const auto v070 = ParseVersion("0.7.0");
  const auto v100 = ParseVersion("1.0.0");
  CHECK(v061 && v061_plain && v0610 && v070 && v100);
  CHECK(*v061 == *v061_plain);
  CHECK(!(*v0610 < *v061));
  CHECK(*v061 < *v0610);
  CHECK(*v0610 < *v070);
  CHECK(*v070 < *v100);
  CHECK(v061->CoreString() == "0.6.1");
  CHECK(v100->ToString() == "1.0.0");

  const auto rc = ParseVersion("0.3.2-rc1");
  const auto rc2 = ParseVersion("0.3.2-rc.2");
  const auto rc10 = ParseVersion("0.3.2-rc.10");
  const auto stable = ParseVersion("0.3.2");
  CHECK(rc && rc2 && rc10 && stable);
  CHECK(*rc < *stable);
  CHECK(*rc2 < *rc10);
  const auto with_build = ParseVersion("0.3.2+abc");
  CHECK(with_build && with_build->build == "abc");
  CHECK(with_build && SemanticVersion::Compare(*with_build, *stable) == 0);
  CHECK(ParseVersion("0.3.2-rc1+build.5")->prerelease.size() == 1);

  CHECK(!ParseVersion("1.0"));
  CHECK(!ParseVersion("1.0.0.0"));
  CHECK(!ParseVersion("01.0.0"));
  CHECK(!ParseVersion("1.0.0-"));
  CHECK(!ParseVersion("v"));
  CHECK(ParseVersion("V0.6.1").has_value());
}

void TestReleases() {
  std::printf("release JSON\n");
  constexpr const char* kJson = R"({
    "tag_name": "v0.7.0",
    "draft": false,
    "prerelease": false,
    "html_url": "https://github.com/saulob/hard-corps-uprising-recompiled/releases/tag/v0.7.0",
    "assets": [
      {"name": "HardCorpsUprising-v0.7.0-windows-x64.zip",
       "browser_download_url": "https://github.com/example/release.zip",
       "size": 123456,
       "digest": "sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
      {"name": "HardCorpsUprising-v0.7.0-windows-x64.sha256",
       "browser_download_url": "https://github.com/example/release.sha256",
       "size": 107,
       "digest": "sha256:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"}
    ]
  })";

  std::string error;
  const std::optional<Release> release = ParseRelease(kJson, &error);
  CHECK(release.has_value());
  CHECK(error.empty());
  if (!release) {
    return;
  }
  CHECK(release->tag == "v0.7.0");
  CHECK(release->version.CoreString() == "0.7.0");
  CHECK(IsInstallableRelease(*release));
  CHECK(release->html_url.rfind("https://", 0) == 0);
  CHECK(release->assets.size() == 2);
  const ReleaseAsset* asset = FindAsset(*release, "HardCorpsUprising-v0.7.0-windows-x64.zip");
  CHECK(asset != nullptr);
  CHECK(asset && asset->size == 123456);
  CHECK(asset && asset->sha256() ==
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(FindAsset(*release, "HardCorpsUprising-v0.7.1-windows-x64.zip") == nullptr);
  CHECK(FindAsset(*release, "different.zip") == nullptr);

  const std::optional<Release> draft = ParseRelease(R"({
    "tag_name":"v0.7.1","draft":true,"prerelease":false,"assets":[]
  })");
  CHECK(draft && !IsInstallableRelease(*draft));
  const std::optional<Release> prerelease = ParseRelease(R"({
    "tag_name":"v0.7.1-rc1","draft":false,"prerelease":true,"assets":[]
  })");
  CHECK(prerelease && !IsInstallableRelease(*prerelease));
  const std::optional<Release> prerelease_tag = ParseRelease(R"({
    "tag_name":"v0.7.1-rc1","draft":false,"prerelease":false,"assets":[]
  })");
  CHECK(prerelease_tag && !IsInstallableRelease(*prerelease_tag));

  CHECK(!ParseRelease(R"({"tag_name": 42})").has_value());
  CHECK(!ParseRelease(R"({"tag_name": "not-a-version"})").has_value());
  CHECK(!ParseRelease("{").has_value());
  CHECK(!ParseRelease(R"({"tag_name":"v0.7.0","assets":{}})").has_value());
  CHECK(!ParseRelease(R"({"tag_name":"v0.7.0","assets":[{"name":"x"}]})").has_value());

  CHECK(ParseSha256Digest("sha256:abc") == std::nullopt);
  CHECK(ParseSha256Digest("md5:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == std::nullopt);
  CHECK(ParseSha256Digest("SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") ==
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(ParseSha256File("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789  file.zip\n") ==
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
  CHECK(ParseSha256File("not-a-hash  file.zip") == std::nullopt);
}

void TestSha256() {
  std::printf("SHA-256\n");
  CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string long_text(1000000, 'a');
  CHECK(Sha256Hex(long_text) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(Sha256Hex("The quick brown fox jumps over the lazy dog") ==
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

  TempDir temp;
  const std::filesystem::path file = temp.path / "hash.bin";
  WriteFile(file, "abc");
  std::string error;
  CHECK(Sha256FileHex(file, error) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(error.empty());
}

void TestZipAndStaging() {
  std::printf("ZIP extraction and staging\n");
  TempDir temp;
  const std::filesystem::path archive = temp.path / "safe.zip";
  const std::string safe = MakeZip({
      {"hard_corps_uprising.exe", "launcher"},
      {"assets/launcher/header-art.png", "art"},
      {"licenses/ReXGlue-LICENSE.txt", "license"},
  });
  CHECK(!safe.empty());
  WriteFile(archive, safe);
  const std::filesystem::path staging = temp.path / "staging";
  std::string error;
  CHECK(ExtractZip(archive, staging, {}, error));
  CHECK(ReadFile(staging / "hard_corps_uprising.exe") == "launcher");
  CHECK(ReadFile(staging / "assets" / "launcher" / "header-art.png") == "art");
  CHECK(ValidateStaging(staging, {"hard_corps_uprising.exe", "assets/launcher/header-art.png"}, error));
  CHECK(ListStagingFiles(staging, error).size() == 3);

  WriteFile(staging / "game" / "default.xex", "game");
  CHECK(!ValidateStaging(staging, {"hard_corps_uprising.exe"}, error));
  std::filesystem::remove_all(staging / "game");

  WriteFile(staging / "settings.toml", "settings");
  CHECK(!ValidateStaging(staging, {"hard_corps_uprising.exe"}, error));
  std::filesystem::remove(staging / "settings.toml");

  WriteFile(staging / "logs" / "a.log", "log");
  CHECK(!ValidateStaging(staging, {"hard_corps_uprising.exe"}, error));
  std::filesystem::remove_all(staging / "logs");

  CHECK(!ValidateStaging(staging, {"missing.exe"}, error));

  const std::vector<std::string> unsafe_names = {
      "../evil",        "C:/evil",         "C:\\evil",       "\\\\server\\share", "a/../../b",
      "a\\..\\b",       "a//b",            "game/default.xex", "settings.toml",     "a/b.log",
      "default.xex",    "C:evil",          "a/b.",            "con",
  };
  int rejected = 0;
  for (const std::string& name : unsafe_names) {
    const std::filesystem::path unsafe_archive = temp.path / "unsafe.zip";
    const std::string bytes = MakeZip({{name, "payload"}});
    if (bytes.empty()) {
      std::printf("  zip writer rejected %s\n", name.c_str());
    }
    CHECK(!bytes.empty());
    WriteFile(unsafe_archive, bytes);
    const std::filesystem::path unsafe_staging = temp.path / "unsafe-staging";
    std::filesystem::remove_all(unsafe_staging);
    if (!ExtractZip(unsafe_archive, unsafe_staging, {}, error)) {
      ++rejected;
    }
    CHECK(!std::filesystem::exists(temp.path / "evil"));
  }
  CHECK(rejected == int(unsafe_names.size()));
  CHECK(IsSymlinkZipEntry(0120000u << 16));
  CHECK(!IsSymlinkZipEntry(0100644u << 16));
  CHECK(!IsSymlinkZipEntry(0));

  const std::filesystem::path dot_archive = temp.path / "dot.zip";
  WriteFile(dot_archive, MakeZip({{"./hard_corps_uprising.exe", "launcher"}}));
  const std::filesystem::path dot_staging = temp.path / "dot-staging";
  CHECK(ExtractZip(dot_archive, dot_staging, {}, error));
  CHECK(std::filesystem::exists(dot_staging / "hard_corps_uprising.exe"));
}

void TestJobs() {
  std::printf("update jobs\n");
  TempDir temp;
  const std::filesystem::path install = temp.path / "install dir";
  const std::filesystem::path staging = temp.path / "staging dir";
  const std::filesystem::path backup = temp.path / "backup dir";
  const std::filesystem::path manifest = temp.path / "manifest.txt";
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(manifest, "hard_corps_uprising.exe\n");

  UpdateJob job;
  job.parent_pid = 1234;
  job.install_dir = install;
  job.staging_dir = staging;
  job.backup_dir = backup;
  job.manifest_file = manifest;
  job.restart_executable = install / "hard_corps_uprising.exe";
  job.log_file = temp.path / "updater.log";
  job.restart_arguments = {"--user_data_root=with space", "--fullscreen=true", ""};

  std::string error;
  const std::filesystem::path job_file = temp.path / "job.txt";
  CHECK(WriteUpdateJob(job_file, job, error));
  const std::optional<UpdateJob> read = ReadUpdateJob(job_file, error);
  CHECK(read.has_value());
  if (read) {
    CHECK(read->parent_pid == 1234);
    CHECK(read->install_dir == install);
    CHECK(read->restart_arguments == job.restart_arguments);
  }

  WriteFile(staging / "hard_corps_uprising.exe", "launcher");
  CHECK(ValidateInstallJob(job, error));
  CHECK(WriteUpdateJob(job_file, job, error));

  UpdateJob inside = job;
  inside.staging_dir = install / "staging";
  std::filesystem::create_directories(inside.staging_dir);
  CHECK(!ValidateInstallJob(inside, error));
}

void TestRollback() {
  std::printf("install and rollback\n");
  TempDir temp;
  const std::filesystem::path install = temp.path / "install";
  const std::filesystem::path staging = temp.path / "staging";
  const std::filesystem::path backup = temp.path / "backup";
  const std::filesystem::path manifest = temp.path / "manifest.txt";
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(install / "hard_corps_uprising.exe", "old launcher");
  WriteFile(install / "rexruntime.dll", "old runtime");
  WriteFile(install / "user_data" / "settings.toml", "player settings");
  WriteFile(install / "game" / "default.xex", "game files");
  WriteFile(install / "logs" / "session.log", "log");

  auto stage = [&] {
    WriteFile(staging / "hard_corps_uprising.exe", "new launcher");
    WriteFile(staging / "trg-updater.exe", "new helper");
    WriteFile(staging / "rexruntime.dll", "new runtime");
    WriteFile(staging / "assets" / "new" / "data.bin", "new nested file");
    WriteFile(manifest, "hard_corps_uprising.exe\ntrg-updater.exe\nrexruntime.dll\nassets/new/data.bin\n");
  };
  stage();

  UpdateJob job;
  job.parent_pid = 1234;
  job.install_dir = install;
  job.staging_dir = staging;
  job.backup_dir = backup;
  job.manifest_file = manifest;
  job.restart_executable = install / "hard_corps_uprising.exe";
  job.log_file = temp.path / "updater.log";

  int replacements = 0;
  InstallOptions fail;
  fail.replace = [&replacements](const std::filesystem::path& from, const std::filesystem::path& to) {
    ++replacements;
    if (replacements == 3) {
      return false;
    }
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
  };
  InstallOutcome failed = InstallFiles(job, fail, false);
  CHECK(failed.status == InstallStatus::kInstallFailedRolledBack);
  CHECK(ReadFile(install / "hard_corps_uprising.exe") == "old launcher");
  CHECK(!std::filesystem::exists(install / "trg-updater.exe"));
  CHECK(ReadFile(install / "rexruntime.dll") == "old runtime");
  CHECK(ReadFile(install / "user_data" / "settings.toml") == "player settings");
  CHECK(ReadFile(install / "game" / "default.xex") == "game files");
  CHECK(ReadFile(install / "logs" / "session.log") == "log");

  stage();
  replacements = 0;
  const InstallOutcome success = InstallFiles(job, {}, false);
  CHECK(success.status == InstallStatus::kSuccess);
  CHECK(ReadFile(install / "hard_corps_uprising.exe") == "new launcher");
  CHECK(ReadFile(install / "trg-updater.exe") == "new helper");
  CHECK(ReadFile(install / "rexruntime.dll") == "new runtime");
  CHECK(ReadFile(install / "assets" / "new" / "data.bin") == "new nested file");
  CHECK(ReadFile(install / "user_data" / "settings.toml") == "player settings");
  CHECK(ReadFile(install / "game" / "default.xex") == "game files");
  CHECK(ReadFile(install / "logs" / "session.log") == "log");
}

void TestPackagingNames() {
  std::printf("packaging names\n");
  const std::string config = ReadFile(std::string(HCU_SRC_DIR) + "/update_config.cpp");
  const std::string windows_package = ReadFile(std::string(HCU_SRC_DIR) + "/../tools/package-release.ps1");
  const std::string linux_package = ReadFile(std::string(HCU_SRC_DIR) + "/../tools/package-linux.sh");
  CHECK(config.find("HardCorpsUprising-v\" + version.CoreString() + \"-windows-x64.zip") != std::string::npos);
  CHECK(config.find("HardCorpsUprisingRecompiled-v\" + version.CoreString() + \"-x86_64.AppImage") !=
        std::string::npos);
  CHECK(windows_package.find("HardCorpsUprising-v$Version-windows-x64") != std::string::npos);
  CHECK(windows_package.find("trg-updater.exe") != std::string::npos);
  CHECK(linux_package.find("HardCorpsUprisingRecompiled-v$VERSION-x86_64") != std::string::npos);
}

void TestPaths() {
  std::printf("path safety\n");
  CHECK(IsSafeRelativePath("hard_corps_uprising.exe"));
  CHECK(IsSafeRelativePath("assets/launcher/header-art.png"));
  CHECK(IsSafeRelativePath("./hard_corps_uprising.exe"));
  CHECK(!IsSafeRelativePath("../hard_corps_uprising.exe"));
  CHECK(!IsSafeRelativePath("/absolute"));
  CHECK(!IsSafeRelativePath("C:/hard_corps_uprising.exe"));
  CHECK(!IsSafeRelativePath("\\\\server\\share"));
  CHECK(!IsSafeRelativePath("a//b"));
  CHECK(!IsSafeRelativePath("a/./../b"));
  CHECK(IsProtectedRelativePath("game/default.xex"));
  CHECK(IsProtectedRelativePath("user_data/settings.toml"));
  CHECK(IsProtectedRelativePath("logs/a.log"));
  CHECK(IsProtectedRelativePath("settings.toml"));
  CHECK(IsProtectedRelativePath("DLC/a.bin"));
  CHECK(!IsProtectedRelativePath("hard_corps_uprising.exe"));
  CHECK(!IsProtectedRelativePath("licenses/THIRD-PARTY-LICENSES.txt"));
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  TestVersions();
  TestReleases();
  TestSha256();
  TestZipAndStaging();
  TestJobs();
  TestRollback();
  TestPackagingNames();
  TestPaths();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
