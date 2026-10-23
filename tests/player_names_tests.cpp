// Player names: the one validator and its edit gate, the launcher keys and page, the SDK values they produce,
// and that the display name never reaches the profile identity (XUID, content directory).
// Run: hcu_player_names_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "player_names.h"

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

using rex::system::xam::AcceptUserDisplayNameEdit;
using rex::system::xam::NormalizeUserDisplayName;
using rex::system::xam::UserDisplayNameResult;

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string ReadSource(const char* name) {
  return ReadFile(std::string(HCU_SRC_DIR) + "/" + name);
}

std::string ReadSdkPatch() {
  return ReadFile(std::string(HCU_SRC_DIR) + "/../patches/rexglue-sdk/0057-local-user-display-names.patch");
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

size_t Count(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

std::string Normalized(const std::string& raw) {
  std::string out = "<rejected>";
  NormalizeUserDisplayName(raw, &out);
  return out;
}

// What the name field holds after typing, pasting or deleting: the proposed text only when it is allowed.
std::string Edit(const std::string& previous, const std::string& proposed, UserDisplayNameResult* result = nullptr) {
  return AcceptUserDisplayNameEdit(previous, proposed, result);
}

const std::string kAccent = "\xC3\xA9";

std::string Repeat(const std::string& unit, int count) {
  std::string out;
  for (int i = 0; i < count; ++i) {
    out += unit;
  }
  return out;
}

void TestValidator() {
  std::printf("validator: 15 characters and 15 bytes in the 16-byte XAM buffer\n");
  CHECK(rex::system::xam::kUserNameBufferSize == 16);
  CHECK(rex::system::xam::kUserDisplayNameMaxBytes == 15);
  CHECK(rex::system::xam::kUserDisplayNameMaxCharacters == 15);
  CHECK(Normalized("Saulo") == "Saulo");
  CHECK(Normalized("  Saulo  ") == "Saulo");
  CHECK(Normalized("") == "<rejected>");
  CHECK(Normalized("    ") == "<rejected>");
  CHECK(Normalized(std::string(15, 'a')) == std::string(15, 'a'));
  CHECK(Normalized(std::string(16, 'a')) == "<rejected>");
  CHECK(Normalized(" " + std::string(15, 'a') + " ") == std::string(15, 'a'));
  CHECK(Normalized(Repeat(kAccent, 7) + "a") == Repeat(kAccent, 7) + "a");
  CHECK(Normalized(Repeat(kAccent, 8)) == "<rejected>");
  CHECK(Normalized("a\xC3") == "<rejected>");
  CHECK(Normalized("\xFF") == "<rejected>");
  CHECK(Normalized("\xC0\x80") == "<rejected>");
  CHECK(Normalized("\xED\xA0\x80") == "<rejected>");
  CHECK(Normalized("\xF4\x90\x80\x80") == "<rejected>");
  CHECK(Normalized(std::string("a\0b", 3)) == "<rejected>");
  CHECK(Normalized("a\tb") == "<rejected>");
  CHECK(Normalized("a\x7F") == "<rejected>");
  CHECK(Normalized("a\xC2\x85") == "<rejected>");
  CHECK(rex::system::xam::Utf8PrefixLength("ab\xC3\xA9", 3) == 2);
  CHECK(rex::system::xam::Utf8PrefixLength("ab\xC3\xA9", 4) == 4);
}

void TestMessages() {
  std::printf("friendly messages\n");
  CHECK(hcu::CheckPlayerName(std::string(15, 'a')) == hcu::PlayerNameResult::Ok);
  CHECK(hcu::CheckPlayerName(std::string(16, 'a')) == hcu::PlayerNameResult::TooManyCharacters);
  CHECK(std::string(hcu::PlayerNameMessage(hcu::PlayerNameResult::TooManyCharacters)) == "Maximum 15 characters.");
  CHECK(hcu::CheckPlayerName(Repeat(kAccent, 8)) == hcu::PlayerNameResult::TooManyBytes);
  CHECK(std::string(hcu::PlayerNameMessage(hcu::PlayerNameResult::TooManyBytes)) ==
        "Name is too long for the game (15-byte limit).");
  CHECK(hcu::CheckPlayerName("a\tb") == hcu::PlayerNameResult::BadCharacter);
  CHECK(hcu::CheckPlayerName("\xFF") == hcu::PlayerNameResult::BadCharacter);
  CHECK(hcu::CheckPlayerName("   ") == hcu::PlayerNameResult::Empty);
  CHECK(std::string(hcu::PlayerNameMessage(hcu::PlayerNameResult::Ok)).empty());
  const std::string hint = hcu::kPlayerNameHint;
  CHECK(hint == "Up to 15 characters.");
  CHECK(!Has(hint, "byte") && !Has(hint, "accent"));
  CHECK(!Has(hcu::PlayerNameMessage(hcu::PlayerNameResult::TooManyBytes), "accented"));
}

void TestEditGate() {
  std::printf("the field never holds an oversized or invalid value\n");
  const std::string fifteen = "123456789012345";
  UserDisplayNameResult result;
  CHECK(Edit("12345678901234", fifteen, &result) == fifteen && result == UserDisplayNameResult::Ok);
  CHECK(Edit(fifteen, fifteen + "6", &result) == fifteen && result == UserDisplayNameResult::TooManyCharacters);
  CHECK(Edit(fifteen, "1234567890X12345", &result) == fifteen);
  const std::string accents = Repeat(kAccent, 7) + "a";
  CHECK(Edit(accents, accents, &result) == accents);
  CHECK(Edit(accents, accents + "a", &result) == accents && result == UserDisplayNameResult::TooManyBytes);
  CHECK(Edit(accents, accents + kAccent, &result) == accents && result == UserDisplayNameResult::TooManyBytes);
  CHECK(Edit(Repeat(kAccent, 7), Repeat(kAccent, 8), &result) == Repeat(kAccent, 7) &&
        result == UserDisplayNameResult::TooManyBytes);
  CHECK(Edit("abc", "abc" + std::string(40, 'x'), &result) == "abc");
  CHECK(Edit("abc", "abc" + Repeat(kAccent, 20), &result) == "abc");
  CHECK(Edit("abcdef", "abXYZef") == "abXYZef");
  CHECK(Edit("abcdef", "aXf") == "aXf");
  CHECK(Edit(fifteen, fifteen.substr(0, 14)) == fifteen.substr(0, 14));
  CHECK(Edit(fifteen, fifteen.substr(1)) == fifteen.substr(1));
  CHECK(Edit(fifteen, fifteen.substr(0, 4) + fifteen.substr(5)) == fifteen.substr(0, 4) + fifteen.substr(5));
  CHECK(Edit(accents, accents.substr(0, accents.size() - 1)) == accents.substr(0, accents.size() - 1));
  CHECK(Edit("abc", "") == "");
  CHECK(Edit("abc", "ab\tc", &result) == "abc" && result == UserDisplayNameResult::BadCharacter);
  CHECK(Edit("abc", "ab\xFF", &result) == "abc" && result == UserDisplayNameResult::BadCharacter);
  CHECK(Edit("abc", "ab\xC3", &result) == "abc");
}

void TestSdkValues() {
  std::printf("SDK values follow logical player 1 and 2\n");
  CHECK(hcu::kPlayerNames.size() == 2);
  CHECK(std::string(hcu::kPlayerNames[0].key) == "hcu_player_1_name");
  CHECK(std::string(hcu::kPlayerNames[1].key) == "hcu_player_2_name");
  CHECK(std::string(hcu::kPlayerNames[0].sdk_key) == "local_player_1_name");
  CHECK(std::string(hcu::kPlayerNames[1].sdk_key) == "local_player_2_name");
  CHECK(std::string(hcu::kPlayerNames[0].default_name) == "Player 1");
  CHECK(std::string(hcu::kPlayerNames[1].default_name) == "Player 2");
  const auto& p1 = hcu::kPlayerNames[0];
  const auto& p2 = hcu::kPlayerNames[1];
  CHECK(hcu::SdkPlayerName(p1, "Saulo") == "Saulo");
  CHECK(p1.send_default && !p2.send_default);
  CHECK(hcu::SdkPlayerName(p2, "Guest") == "Guest");
  CHECK(hcu::SdkPlayerName(p1, "  Saulo ") == "Saulo");
  CHECK(hcu::SdkPlayerName(p1, "Player 1") == "Player 1");
  CHECK(hcu::SdkPlayerName(p1, "") == "Player 1");
  CHECK(hcu::SdkPlayerName(p1, "\xFF") == "Player 1");
  CHECK(hcu::SdkPlayerName(p1, std::string(16, 'a')) == "Player 1");
  CHECK(hcu::SdkPlayerName(p1, Repeat(kAccent, 8)) == "Player 1");
  CHECK(hcu::SdkPlayerName(p2, "Player 2").empty());
  CHECK(hcu::SdkPlayerName(p2, std::string(16, 'a')).empty());
  CHECK(hcu::SdkPlayerName(p2, "\xFF").empty());
  CHECK(hcu::SdkPlayerName(p2, "a\tb").empty());
  CHECK(hcu::SdkPlayerName(p1, std::string(15, 'a')) == std::string(15, 'a'));
}

void TestSettingsAndLauncher() {
  std::printf("two settings, saved defensively, one section on the Gameplay page\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_STRING(hcu_player_1_name, \"Player 1\""));
  CHECK(Has(settings, "REXCVAR_DEFINE_STRING(hcu_player_2_name, \"Player 2\""));
  CHECK(Has(settings, "\"hcu_player_1_name\","));
  CHECK(Has(settings, "\"hcu_player_2_name\","));
  CHECK(!Has(settings, "player_3") && !Has(settings, "player_4"));
  CHECK(!Has(settings, "ssf2t_") && !Has(settings, "gw2_"));
  CHECK(Has(settings, "!IsValidPlayerName(name->second)"));
  CHECK(Has(settings, "name->second = player.default_name;"));
  CHECK(Has(settings, "SetFlagByName(player.sdk_key, SdkPlayerName("));
  CHECK(Has(ReadSource("hard_corps_uprising_app.h"), "hcu::ApplyPlayerNames();"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Count(launcher, "ui.Section(\"Player names\"") == 1);
  const size_t gameplay = launcher.find("static void PageGameplay");
  const size_t cheats = launcher.find("static void PageCheats");
  const size_t controls = launcher.find("static void PageControls");
  const size_t names = launcher.find("ui.Section(\"Player names\"");
  CHECK(gameplay < names && names < cheats);
  CHECK(controls > cheats);
  CHECK(launcher.find("ui.Section(\"Player names\"", controls) == std::string::npos);
  CHECK(Has(launcher, "ui.Help(kPlayerNamesHelp);"));
  CHECK(std::string(hcu::kPlayerNamesHelp) == "Changes the name shown in-game. Save data is not affected.");
  CHECK(Has(launcher, "ImGuiInputTextFlags_CallbackEdit"));
  CHECK(Has(launcher, "AcceptUserDisplayNameEdit"));
  CHECK(!Has(launcher, "player_3") && !Has(launcher, "player_4"));
  CHECK(!Has(launcher, "ssf2t_") && !Has(launcher, "gw2_"));
}

void TestIdentityUnchanged() {
  std::printf("renaming never reaches the XUID or the content directory\n");
  const std::string patch = ReadSdkPatch();
  CHECK(Has(patch, "+  const std::string& profile_id() const { return name_; }"));
  CHECK(Has(patch, "-  auto user_name = rex::to_path(kernel_state_->user_profile()->name());"));
  CHECK(Has(patch, "+  auto user_name = rex::to_path(kernel_state_->user_profile()->profile_id());"));
  CHECK(!Has(patch, "xuid_ ="));
  CHECK(!Has(patch, "+  name_ ="));
  CHECK(Has(patch, "+  *name = name_;"));
  CHECK(Has(patch, "user_profile->display_name(user_index, &name)"));
  CHECK(!Has(patch, "+  info->signin_state") && !Has(patch, "-  info->signin_state"));
  CHECK(!Has(patch, "XamUserGetSigninState"));
  CHECK(Has(patch, "local_player_1_name") && Has(patch, "local_player_2_name") &&
        Has(patch, "local_player_3_name") && Has(patch, "local_player_4_name"));
  CHECK(!Has(patch, "hcu_"));
}

void TestSharedValidator() {
  std::printf("the project validator is the SDK patch's header, byte for byte\n");
  const std::string patch = ReadSdkPatch();
  const std::string start = "+++ b/include/rex/system/xam/user_display_name.h\n";
  size_t at = patch.find(start);
  CHECK(at != std::string::npos);
  at = patch.find('\n', patch.find("@@", at)) + 1;
  const size_t end = patch.find("\ndiff --git", at);
  std::string added;
  std::istringstream lines(patch.substr(at, end - at));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line[0] == '+') {
      added += line.substr(1) + "\n";
    }
  }
  std::string project;
  for (char c : ReadSource("user_display_name.h")) {
    if (c != '\r') {
      project += c;
    }
  }
  CHECK(!added.empty() && added == project);
}

void TestInputUntouched() {
  std::printf("title START and player assignment code is not part of this feature\n");
  const std::string patch = ReadSdkPatch();
  CHECK(!Has(patch, "input"));
  CHECK(!Has(ReadSource("player_names.h"), "title_start"));
  CHECK(!Has(ReadSource("player_names.h"), "xuid"));
  CHECK(Has(ReadSource("title_start.h"), "ShouldOverrideTitleStart"));
  CHECK(!Has(ReadSdkPatch(), "SigninState"));
  CHECK(!Has(ReadSdkPatch(), "title_start"));
}

}  // namespace

int main() {
  TestValidator();
  TestMessages();
  TestEditGate();
  TestSdkValues();
  TestSettingsAndLauncher();
  TestIdentityUnchanged();
  TestSharedValidator();
  TestInputUntouched();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
