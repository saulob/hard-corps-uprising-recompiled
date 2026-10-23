#pragma once

#include <array>
#include <string>
#include <string_view>

#include "user_display_name.h"

namespace hcu {

struct PlayerNameSetting {
  const char* key;
  const char* sdk_key;
  const char* label;
  const char* default_name;
  bool send_default;
};

inline constexpr char kPlayerNameHint[] = "Up to 15 characters.";
inline constexpr char kPlayerNamesHelp[] = "Changes the name shown in-game. Save data is not affected.";

inline constexpr std::array<PlayerNameSetting, 2> kPlayerNames = {{
    {"hcu_player_1_name", "local_player_1_name", "Player 1", "Player 1", true},
    {"hcu_player_2_name", "local_player_2_name", "Player 2", "Player 2", false},
}};

using PlayerNameResult = rex::system::xam::UserDisplayNameResult;

inline bool IsValidPlayerName(std::string_view text) {
  return rex::system::xam::NormalizeUserDisplayName(text, nullptr);
}

inline PlayerNameResult CheckPlayerName(std::string_view raw) {
  const std::string_view trimmed = rex::system::xam::TrimUserDisplayName(raw);
  return rex::system::xam::MeasureUserDisplayName(trimmed);
}

// The value for the SDK's local_player_N_name cvar. An invalid name becomes the default. A user that is not signed
// in only gets a name once it differs from the default; empty leaves the SDK answering "no such user".
inline std::string SdkPlayerName(const PlayerNameSetting& player, std::string_view launcher_value) {
  std::string name;
  if (!rex::system::xam::NormalizeUserDisplayName(launcher_value, &name)) {
    name = player.default_name;
  }
  if (!player.send_default && name == player.default_name) {
    return {};
  }
  return name;
}

inline const char* PlayerNameMessage(PlayerNameResult result) {
  switch (result) {
    case PlayerNameResult::Empty:
      return "Enter a name.";
    case PlayerNameResult::TooManyCharacters:
      return "Maximum 15 characters.";
    case PlayerNameResult::TooManyBytes:
      return "Name is too long for the game (15-byte limit).";
    case PlayerNameResult::BadCharacter:
      return "That name has a character that cannot be used.";
    default:
      return "";
  }
}

}  // namespace hcu
