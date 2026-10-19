#include "launcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/version.h>

#include "trg/headers.h"
#include "trg/launcher.h"
#include "trg/platform.h"

#include "aspect_ratio.h"
#include "game_files.h"
#include "graphics_adapters.h"
#include "imgui_gamepad.h"
#include "internal_render_scale.h"
#include "keybinds.h"
#include "launcher_achievements.h"
#include "launcher_quit.h"
#include "platform.h"
#include "settings.h"

namespace hcu {
namespace {

// Logical pixels.
constexpr std::pair<int, int> kLauncherWindowSize = {1280, 720};
// Share of the primary display's work area the launcher may take.
constexpr double kLauncherMaxShare = 0.9;
// How long the saved message stays on screen before the game starts.
constexpr double kPlayFeedbackSeconds = 0.4;

constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

// Hard Corps HUD look: neon green over the Midnight background.
const ImVec4 kAccent = ImVec4(0.36f, 0.88f, 0.44f, 1.0f);
const ImVec4 kEmerald = ImVec4(0.09f, 0.36f, 0.22f, 1.0f);

trg::Theme LauncherTheme() {
  trg::Theme theme = trg::Theme::Midnight().WithAccent(kAccent);
  theme.good = kEmerald;
  theme.good_dot = kAccent;
  return theme;
}

trg::HeaderPainter LauncherHeader() {
  return trg::headers::Gradient(IM_COL32(14, 18, 30, 255), IM_COL32(10, 52, 32, 255), IM_COL32(8, 28, 18, 255),
                                IM_COL32(8, 10, 18, 255));
}

trg::Fonts g_fonts;

GameFiles CheckAndLogGameFiles(const std::filesystem::path& game_dir) {
  const GameCheck check = CheckGameFiles(game_dir);
  if (check.state == GameFiles::kUnsupported && check.xex_crc32 != 0) {
    REXLOG_WARN("default.xex is not the supported version (CRC32 {:08X})", check.xex_crc32);
  }
  return check.state;
}

// Written by the install job, read once the Task has finished.
struct InstallOutcome {
  InstallResult result;
  GameFiles files = GameFiles::kMissing;
};

struct DlcOutcome {
  bool removal = false;
  std::string name;  // the package's display name, for the status line
  InstallResult install;
  RemoveResult removal_result;
  DlcInfo dlc;
};

std::string DlcDisplayName(const DlcInfo& dlc) {
  return dlc.character != DlcCharacter::kUnknown ? DlcCharacterName(dlc.character) : dlc.display_name;
}

std::string InstalledDlcMessage(const DlcInfo& dlc) {
  if (dlc.character != DlcCharacter::kUnknown) {
    return DlcDisplayName(dlc) + " installed.";
  }
  return dlc.display_name + " installed. It is not one of the supported characters.";
}

// The theme's only alert colours; destructive actions use them instead of the accent.
void PushDangerButton(const trg::Theme& t) {
  ImGui::PushStyleColor(ImGuiCol_Button, t.warn);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.warn.x + 0.12f, t.warn.y + 0.10f, t.warn.z + 0.05f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.warn.x - 0.05f, t.warn.y - 0.04f, t.warn.z - 0.02f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, t.text);
}

std::string Size(int width, int height) {
  return std::to_string(width) + " \xC3\x97 " + std::to_string(height);
}

std::vector<trg::Option> RenderResolutionOptions(AspectRatio aspect) {
  std::vector<trg::Option> options;
  for (int scale = 1; scale <= 4; ++scale) {
    std::string label = scale == 1 ? "Original" : std::to_string(scale) + "x";
    label += " \xE2\x80\x94 " + std::to_string(GuestWidth(aspect) * scale) + "x" + std::to_string(kGuestHeight * scale);
    if (scale == 2) {
      label += " \xE2\x80\x94 Recommended for 1440p";
    }
    options.push_back({std::to_string(scale), label});
  }
  return options;
}

std::vector<trg::Option> AspectRatioOptions() {
  std::vector<trg::Option> options;
  for (const AspectRatioInfo& info : kAspectRatios) {
    options.push_back({info.key, info.label});
  }
  return options;
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                 LauncherCallbacks callbacks)
      : ImGuiDialog(drawer),
        paths_(std::move(paths)),
        cb_(std::move(callbacks)),
        settings_(settings),
        game_files_(CheckAndLogGameFiles(paths_.game_dir)),
        monitors_(ListMonitors()),
        graphics_adapters_(ListGraphicsAdapters()) {
    dlc_ = ListInstalledDlc(paths_.user_dir);
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
    if (WindowFullscreen()) {
      restore_when_windowed_ = true;
      RequestFullscreen(false);
    } else {
      StartInSavedState();
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) {
      return;
    }
    PollInstall();
    PollDlcTask();
    TrackPlacement();
    if (!nav_taken_) {
      saved_nav_flags_ = io.ConfigFlags & kNavFlags;
      saved_has_gamepad_ = io.BackendFlags & ImGuiBackendFlags_HasGamepad;
      io.ConfigFlags |= kNavFlags;
      nav_taken_ = true;
    }
    gamepad_.Update(io);
    if (!starting_ && ImGui::IsKeyPressed(ImGuiKey_F11, false) && !fullscreen_gate_.Pending()) {
      ToggleLauncherFullscreen();
    }
    if (starting_ && ImGui::GetTime() - play_started_ >= kPlayFeedbackSeconds && !LeavingFullscreen()) {
      StartGame();
      return;
    }
    if (starting_) {
      // TRG's hotkeys read the same key state, so release it before Frame() runs.
      io.ClearInputKeys();
      io.ClearInputMouse();
    }
    ImGui::BeginDisabled(starting_);
    const trg::Result result = launcher_->Frame();
    ImGui::EndDisabled();
    if (starting_) {
      DrawStartingMessage();
    }
    switch (result) {
      case trg::Result::kPlay:
        RequestPlay();
        break;
      case trg::Result::kQuit:
        if (!starting_ && hcu::LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
          Finish(cb_.quit);
        }
        break;
      case trg::Result::kNone:
        break;
    }
  }

  // Every PLAY (mouse, Enter, Xbox Start) ends here. The one save happens here; StartGame never saves.
  void RequestPlay() {
    if (starting_ || install_.running() || dlc_task_.running() || game_files_ != GameFiles::kReady) {
      return;
    }
    if (!launcher_->Save()) {
      return;
    }
    starting_ = true;
    play_started_ = ImGui::GetTime();
    launcher_->SetStatus("", 0.0);
  }

  void StartGame() {
    // The game builds its own runtime next; the one that read the achievements must be gone.
    achievements_.reset();
    achievements_tried_ = false;
    if (cb_.window_changed && cb_.window_changed()) {
      if (cb_.restart && cb_.restart()) {
        Finish(nullptr);
        return;
      }
      starting_ = false;
      launcher_->SetStatus("Could not start the game in a new window. Press Play to try again.", 8);
      return;
    }
    RestoreStartupWindow();
    Finish(cb_.play);
  }

  // Drawn outside the disabled frame so the message keeps full brightness.
  void DrawStartingMessage() {
    constexpr const char* kText = "Settings saved. Starting game...";
    ImFont* font = g_fonts.semibold ? g_fonts.semibold : ImGui::GetFont();
    const float size = g_fonts.size * 1.25f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kText);
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - text_size.x) * 0.5f,
                     vp->Pos.y + (vp->Size.y - text_size.y) * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(font, size, pos, ImGui::GetColorU32(kAccent), kText);
  }

  // Runs inside the last frame; the in-game overlays share this ImGui context.
  void OnClose() override {
    ImGuiIO& io = GetIO();
    gamepad_.Release(io);
    io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
    io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
  }

 private:
  void Finish(std::function<void()> action) {
    done_ = true;
    SavePlacement(placement_.Flush());
    Close();
    if (action) {
      action();
    }
  }

  void* WindowHandle() const { return cb_.window_handle ? cb_.window_handle() : nullptr; }

  bool WindowFullscreen() const { return cb_.window_fullscreen && cb_.window_fullscreen(); }

  void StartInSavedState() {
    RestorePlacement();
    if (fullscreen_pref_.wanted()) {
      RequestFullscreen(true);
    }
  }

  void ToggleLauncherFullscreen() {
    const bool fullscreen = !WindowFullscreen();
    RequestFullscreen(fullscreen);
    fullscreen_pref_.UserSet(fullscreen);
    if (!settings_.SaveNow(LauncherFullscreenValues(fullscreen))) {
      REXLOG_WARN("Could not save the launcher fullscreen state");
    }
  }

  void RequestFullscreen(bool fullscreen) {
    fullscreen_gate_.Request(fullscreen);
    if (cb_.set_window_fullscreen) {
      cb_.set_window_fullscreen(fullscreen);
    }
  }

  // The game gets the window out of the launcher's temporary fullscreen.
  bool LeavingFullscreen() {
    if (!WindowFullscreen() || leave_gave_up_) {
      return false;
    }
    if (!leave_requested_) {
      leave_requested_ = true;
      leave_started_ = ImGui::GetTime();
      RequestFullscreen(false);
    }
    leave_gave_up_ = ImGui::GetTime() - leave_started_ >= FullscreenGate::kTimeoutSeconds;
    return !leave_gave_up_;
  }

  void RestorePlacement() {
    if (const auto placed = TakePreShowResult()) {
      startup_ = placed->startup;
      placement_ = PlacementTracker(placed->applied);
      return;
    }
    void* window = WindowHandle();
    const auto state = QueryWindowState(window);
    if (!state || WindowFullscreen()) {
      return;
    }
    const WindowPlacement saved = SavedLauncherPlacement();
    const bool restorable = PlacementIsVisible(saved, WorkAreas());
    startup_ = WindowPlacement{state->rect, state->maximized};
    placement_ = PlacementTracker(restorable ? std::optional<WindowPlacement>(saved) : std::nullopt);
    if (restorable) {
      ApplyWindowPlacement(window, saved);
    }
  }

  void TrackPlacement() {
    const double now = ImGui::GetTime();
    const FullscreenGate::Step step = fullscreen_gate_.Update(WindowFullscreen(), now);
    if (restore_when_windowed_) {
      if (!WindowFullscreen() && !fullscreen_gate_.Pending()) {
        restore_when_windowed_ = false;
        StartInSavedState();
      }
      return;
    }
    if (!startup_) {
      return;
    }
    if (step.restore) {
      if (const auto current = placement_.Current()) {
        ApplyWindowPlacement(WindowHandle(), *current);
      }
    }
    if (!step.observe) {
      return;
    }
    if (const auto state = QueryWindowState(WindowHandle())) {
      placement_.Observe(*state, WindowFullscreen(), now);
      SavePlacement(placement_.TakeSettled(now));
    }
  }

  void SavePlacement(const std::optional<WindowPlacement>& placement) {
    if (placement && !settings_.SaveNow(PlacementValues(*placement))) {
      REXLOG_WARN("Could not save the launcher window placement");
    }
  }

  // The game keeps the window it was created with, not the launcher's saved placement.
  void RestoreStartupWindow() {
    if (startup_ && !WindowFullscreen()) {
      ApplyWindowPlacement(WindowHandle(), *startup_);
    }
  }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    // PLAY saves in RequestPlay, so a failed save keeps the launcher open.
    c.save_on_play = false;
    c.theme = LauncherTheme();
    c.note = "Play applies the changes. Save keeps them without starting.";
    c.hints = "Enter  Play     Alt+F4  Quit     Ctrl+S  Save";
    c.fonts = g_fonts;
    c.branding.title = "HARD CORPS: UPRISING";
    c.branding.background = LauncherHeader();
    c.pages = {
        {"Play", "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
         [this] { return game_files_ != GameFiles::kReady; }},
        {"DLC", "Optional character downloadable content", [this](trg::Ui& ui) { PageDlc(ui); }},
        {"Display", "Window, monitor and how the picture fits your screen.",
         [this](trg::Ui& ui) { PageDisplay(ui); }},
        {"Graphics", "Render resolution, anti-aliasing and texture filtering.",
         [this](trg::Ui& ui) { PageGraphics(ui); }},
        {"Gameplay", "What the window shows while you play.", [](trg::Ui& ui) { PageGameplay(ui); }},
        {"Cheats", "Optional gameplay modifiers", [](trg::Ui& ui) { PageCheats(ui); }},
        {"Achievements", "Your achievements on this PC.", [this](trg::Ui& ui) { PageAchievements(ui); }},
        {"Controls", "Controller and keyboard.", [](trg::Ui& ui) { PageControls(ui); }},
        {"About", "About this port, and where your files and settings live.",
         [this](trg::Ui& ui) { PageAbout(ui); }},
    };
    c.can_play = [this] {
      if (install_.running() || dlc_task_.running()) {
        return trg::PlayCheck{false, "Wait for the install to finish.", 0};
      }
      if (game_files_ == GameFiles::kReady) {
        return trg::PlayCheck{};
      }
      return trg::PlayCheck{false, "The game files are not ready: see the Play page.", 0};
    };
    return c;
  }

  void PagePlay(trg::Ui& ui) {
    const std::string dir = Utf8(paths_.game_dir);
    if (install_.running()) {
      ui.StatusCard(trg::Status::kBusy, "Installing game files", nullptr, std::max(0.0f, install_.fraction()));
    } else {
      switch (game_files_) {
        case GameFiles::kReady:
          ui.StatusCard(trg::Status::kReady, "Ready to play", dir.c_str());
          break;
        case GameFiles::kUnsupported:
          ui.StatusCard(trg::Status::kAttention, "Different version of the game",
                        DescribeInstall(InstallStatus::kWrongVersion).text);
          break;
        case GameFiles::kMissing:
          ui.StatusCard(trg::Status::kAttention, "Game files needed",
                        "This port requires your own Hard Corps: Uprising Xbox Live Arcade package.");
          break;
      }
    }
    if (!install_failure_.empty()) {
      DrawFailure(ui, install_failure_, install_failure_text_);
    }
    if (paths_.game_dir.empty()) {
      ui.Help("No game folder was given: start the game with --game_data_root.");
    } else {
      ui.Row("Game package",
             "Title ID: 58410A18. No game files are included with this port; the package's files (about 1 GB) are "
             "copied next to the program.");
      if (!ui.TaskProgress(install_)) {
        const bool clicked = game_files_ == GameFiles::kReady
                                 ? ImGui::Button("Reinstall from package...", ImVec2(-FLT_MIN, 0))
                                 : ui.AccentButton("Install from package...");
        if (clicked) {
          BrowseAndInstall();
        }
      }
    }
    ui.LauncherVisibilityRow("hcu_launcher");
    ui.EndRows();
  }

  static void DrawFailure(trg::Ui& ui, const std::string& title, const std::string& text) {
    ui.Spacer(4);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ui.Help(text.c_str());
    ui.Spacer(4);
  }

  void BrowseAndInstall() {
    PollInstall();
    if (install_.running()) {
      return;
    }
    const std::string picked = trg::BrowseForFile("Select your Hard Corps: Uprising Xbox Live Arcade package",
                                                  {{"Xbox Live Arcade package", "*"}});
    if (picked.empty()) {
      return;
    }
    install_failure_.clear();
    install_failure_text_.clear();
    achievements_.reset();
    achievements_tried_ = false;
    outcome_ = std::make_shared<InstallOutcome>();
    const bool started = install_.Start(
        "Reading package...",
        [outcome = outcome_, package = PathFromUtf8(picked), game_dir = paths_.game_dir](trg::Task& task) {
          InstallHooks hooks;
          hooks.phase = [&task](const char* text) {
            task.SetLabel(text);
            task.Progress(0, 0);
          };
          hooks.progress = [&task](uint64_t done, uint64_t total) {
            task.Progress(static_cast<long long>(done), static_cast<long long>(total));
            return !task.cancelled();
          };
          outcome->result = InstallFromPackage(package, game_dir, hooks);
          if (outcome->result.ok()) {
            outcome->files = CheckGameFiles(game_dir).state;
          }
          return outcome->result.ok() ? std::string() : std::string(DescribeInstall(outcome->result.status).title);
        });
    if (started) {
      REXLOG_INFO("Installing game files from {}", picked);
    }
  }

  // Runs every frame so a finished install is picked up whichever page is open.
  void PollInstall() {
    std::string error;
    if (install_.Finish(&error) == trg::Task::State::kIdle) {
      return;
    }
    const std::shared_ptr<InstallOutcome> outcome = std::move(outcome_);
    if (!outcome) {
      return;
    }
    const InstallResult& result = outcome->result;
    const InstallMessage message = DescribeInstall(result.status);
    if (result.ok() || result.status == InstallStatus::kCancelled) {
      if (result.ok()) {
        game_files_ = outcome->files;
        REXLOG_INFO("Game files installed: {} files, {} bytes", result.files, result.bytes);
      } else {
        REXLOG_INFO("Game install cancelled");
      }
      launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
      return;
    }
    REXLOG_WARN("Game install failed: {} ({})", message.title, result.detail);
    install_failure_ = message.title;
    install_failure_text_ = message.text;
    launcher_->SetStatus("Install failed: " + install_failure_, 8.0);
  }

  void PageDlc(trg::Ui& ui) {
    ui.Paragraph(
        "Install optional character DLC from your own Xbox 360 packages.\n"
        "No DLC files are included with this port.\n"
        "Installed packages are stored separately from the game files.");
    ui.Spacer(8);
    const bool busy = dlc_task_.running();
    for (const DlcCharacter character : kDlcCharacters) {
      const DlcInfo* installed = FindInstalledDlc(character);
      if (DrawDlcCard(ui, DlcCharacterName(character), installed, busy)) {
        RequestDlcRemoval(*installed);
      }
    }
    ui.Spacer(6);
    if (!dlc_removing_name_.empty()) {
      ui.Help(("Removing " + dlc_removing_name_ + "...").c_str());
    } else if (!ui.TaskProgress(dlc_task_) && ui.AccentButton("Install DLC...")) {
      BrowseAndInstallDlc();
    }
    if (!dlc_failure_.empty()) {
      DrawFailure(ui, dlc_failure_, dlc_failure_text_);
    }
    const std::vector<const DlcInfo*> others = UnassignedInstalledDlc();
    if (!others.empty() && ui.Section("Other installed content", true)) {
      for (const DlcInfo* dlc : others) {
        if (DrawDlcCard(ui, DlcDisplayName(*dlc), dlc, busy)) {
          RequestDlcRemoval(*dlc);
        }
      }
    }
    DrawRemoveDialog(ui);
  }

  const DlcInfo* FindInstalledDlc(DlcCharacter character) const {
    for (const DlcInfo& dlc : dlc_) {
      if (dlc.verified && dlc.character == character) {
        return &dlc;
      }
    }
    return nullptr;
  }

  // Verified packages that no character row shows: other content, and a second package for a character.
  std::vector<const DlcInfo*> UnassignedInstalledDlc() const {
    std::vector<const DlcInfo*> others;
    for (const DlcInfo& dlc : dlc_) {
      bool shown = false;
      for (const DlcCharacter character : kDlcCharacters) {
        shown |= FindInstalledDlc(character) == &dlc;
      }
      if (dlc.verified && !shown) {
        others.push_back(&dlc);
      }
    }
    return others;
  }

  // Returns true when Remove was clicked.
  bool DrawDlcCard(trg::Ui& ui, const std::string& title, const DlcInfo* installed, bool busy) {
    const trg::Theme& t = ui.theme();
    const float s = ui.scale();
    const float height = 66 * s;
    const float pad = 18 * s;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::Dummy(ImVec2(width, height));
    const ImVec2 next = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImVec4(t.frame.x, t.frame.y, t.frame.z, 0.5f)), 10 * s);

    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    const float title_h = ImGui::GetTextLineHeight();
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(origin.x + pad, origin.y + 12 * s),
                ImGui::GetColorU32(t.text), title.c_str());
    ImGui::PopFont();

    ImGui::PushFont(ui.fonts().regular, ui.fonts().size * 0.84f);
    const float status_y = origin.y + 12 * s + title_h + 2 * s;
    if (installed) {
      const ImU32 green = ImGui::GetColorU32(t.good_dot);
      dl->AddCircleFilled(ImVec2(origin.x + pad + 4 * s, status_y + ImGui::GetFontSize() * 0.5f), 4 * s, green);
      dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(origin.x + pad + 14 * s, status_y), green,
                  "Installed");
    } else {
      dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(origin.x + pad, status_y),
                  ImGui::GetColorU32(t.dim), "Not installed");
    }
    ImGui::PopFont();

    bool remove_clicked = false;
    if (installed) {
      const float button_w = 120 * s;
      const float button_h = ImGui::GetFrameHeight();
      ImGui::SetCursorScreenPos(ImVec2(origin.x + width - pad - button_w, origin.y + (height - button_h) * 0.5f));
      ImGui::PushID(installed->content_id.c_str());
      ImGui::BeginDisabled(busy);
      PushDangerButton(t);
      remove_clicked = ImGui::Button("Remove", ImVec2(button_w, button_h));
      ImGui::PopStyleColor(4);
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(next);
    return remove_clicked;
  }

  void RequestDlcRemoval(const DlcInfo& dlc) {
    dlc_remove_target_ = dlc;
    dlc_remove_open_ = true;
  }

  void DrawRemoveDialog(trg::Ui& ui) {
    if (!dlc_remove_target_) {
      return;
    }
    constexpr const char* kId = "##dlc_remove";
    if (dlc_remove_open_) {
      ImGui::OpenPopup(kId);
      dlc_remove_open_ = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = ui.scale();
    const trg::Theme& t = ui.theme();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(520 * s, vp->Size.x - 40 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * s, 20 * s));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, t.warn);
    const bool open = ImGui::BeginPopupModal(kId, nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    if (!open) {
      dlc_remove_target_.reset();
      return;
    }

    const DlcInfo target = *dlc_remove_target_;
    ImGui::PushFont(ui.fonts().semibold, ui.fonts().size * 1.1f);
    ImGui::TextUnformatted(("Remove " + DlcDisplayName(target) + " DLC").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 6 * s));
    ImGui::PushStyleColor(ImGuiCol_Text, t.dim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(
        "This removes the installed DLC files from this PC.\nYou can install the package again later.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 14 * s));

    bool cancel = ImGui::Button("Cancel", ImVec2(150 * s, 0));
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    PushDangerButton(t);
    const bool remove = ImGui::Button("Remove", ImVec2(150 * s, 0));
    ImGui::PopStyleColor(4);

    cancel |= ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
              ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (remove) {
      dlc_remove_target_.reset();
      ImGui::CloseCurrentPopup();
      StartDlcRemoval(target);
    } else if (cancel) {
      dlc_remove_target_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  void BrowseAndInstallDlc() {
    PollDlcTask();
    if (dlc_task_.running()) {
      return;
    }
    const std::string picked = trg::BrowseForFile("Select a Hard Corps: Uprising DLC package",
                                                  {{"Xbox 360 package", "*"}});
    if (picked.empty()) {
      return;
    }
    dlc_failure_.clear();
    dlc_failure_text_.clear();
    dlc_outcome_ = std::make_shared<DlcOutcome>();
    const bool started = dlc_task_.Start(
        "Reading package...",
        [outcome = dlc_outcome_, package = PathFromUtf8(picked), user_dir = paths_.user_dir](trg::Task& task) {
          InstallHooks hooks;
          hooks.phase = [&task](const char* text) {
            task.SetLabel(text);
            task.Progress(0, 0);
          };
          hooks.progress = [&task](uint64_t done, uint64_t total) {
            task.Progress(static_cast<long long>(done), static_cast<long long>(total));
            return !task.cancelled();
          };
          outcome->install = InstallDlcFromPackage(package, user_dir, hooks, &outcome->dlc);
          return outcome->install.ok() ? std::string() : std::string(DescribeInstall(outcome->install.status).title);
        });
    if (started) {
      REXLOG_INFO("Installing DLC from {}", picked);
    }
  }

  void StartDlcRemoval(const DlcInfo& dlc) {
    PollDlcTask();
    if (dlc_task_.running()) {
      return;
    }
    dlc_failure_.clear();
    dlc_failure_text_.clear();
    dlc_outcome_ = std::make_shared<DlcOutcome>();
    dlc_outcome_->removal = true;
    dlc_outcome_->name = DlcDisplayName(dlc);
    const bool started = dlc_task_.Start(
        "Removing DLC...",
        [outcome = dlc_outcome_, user_dir = paths_.user_dir, game_dir = paths_.game_dir,
         content_id = dlc.content_id](trg::Task&) {
          outcome->removal_result = RemoveInstalledDlc(user_dir, game_dir, content_id);
          return outcome->removal_result.ok() ? std::string()
                                              : std::string(DescribeRemove(outcome->removal_result.status).title);
        });
    if (started) {
      dlc_removing_name_ = dlc_outcome_->name;
      REXLOG_INFO("Removing DLC {}", dlc.content_id);
    } else {
      dlc_outcome_.reset();
    }
  }

  // Runs every frame: finishes the DLC task and reloads the installed packages.
  void PollDlcTask() {
    std::string error;
    if (dlc_task_.Finish(&error) == trg::Task::State::kIdle) {
      return;
    }
    dlc_removing_name_.clear();
    const std::shared_ptr<DlcOutcome> outcome = std::move(dlc_outcome_);
    dlc_ = ListInstalledDlc(paths_.user_dir);
    if (!outcome) {
      return;
    }
    if (outcome->removal) {
      FinishDlcRemoval(*outcome);
    } else {
      FinishDlcInstall(*outcome);
    }
  }

  void FinishDlcInstall(const DlcOutcome& outcome) {
    const InstallResult& result = outcome.install;
    const InstallMessage message = DescribeInstall(result.status);
    if (result.ok()) {
      REXLOG_INFO("DLC installed: {} ({} files, {} bytes)", outcome.dlc.display_name, result.files, result.bytes);
      launcher_->SetStatus(InstalledDlcMessage(outcome.dlc), 6.0);
      return;
    }
    if (result.status == InstallStatus::kCancelled) {
      REXLOG_INFO("DLC install cancelled");
      launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
      return;
    }
    REXLOG_WARN("DLC install failed: {} ({})", message.title, result.detail);
    dlc_failure_ = message.title;
    dlc_failure_text_ = message.text;
    launcher_->SetStatus("DLC install failed: " + dlc_failure_, 8.0);
  }

  void FinishDlcRemoval(const DlcOutcome& outcome) {
    const RemoveResult& result = outcome.removal_result;
    if (result.ok()) {
      REXLOG_INFO("DLC removed: {}", outcome.name);
      launcher_->SetStatus(outcome.name + " removed. You can install the package again later.", 6.0);
      return;
    }
    const RemoveMessage message = DescribeRemove(result.status);
    REXLOG_WARN("DLC removal failed: {} ({})", message.title, result.detail);
    dlc_failure_ = message.title;
    dlc_failure_text_ = message.text;
    launcher_->SetStatus("Removal failed: " + dlc_failure_, 8.0);
  }

  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Display mode",
              "How the game starts. Borderless Fullscreen fills the monitor. F11 switches while playing.", "fullscreen",
              false, "Windowed", "Borderless Fullscreen");
    ui.Row("Window size", "The game window's size in Windowed mode.");
    WindowSizeCombo();
    ui.Row("Monitor", "The display the game opens on.");
    MonitorCombo();
    if (ui.Combo("Aspect Ratio",
                 "Sets the game's target rendering format and the Window size choices.",
                 "hcu_aspect_ratio", "16:9", AspectRatioOptions())) {
      SelectWindowSizeForAspect();
    }
    ui.Row("VSync", "On prevents tearing. Off allows it, for the lowest latency and G-Sync/FreeSync.");
    {
      const bool tearing = settings_.GetBool(kTearingSetting, true);
      if (int i = ui.Segmented("vsync", {"Off", "On"}, tearing ? 0 : 1); i >= 0) {
        settings_.SetBool(kTearingSetting, i == 0);
      }
    }
  }

  const MonitorInfo* SelectedMonitor() const {
    const int index = settings_.GetInt("monitor", 0);
    if (index >= 1 && index <= int(monitors_.size())) {
      return &monitors_[size_t(index - 1)];
    }
    for (const MonitorInfo& m : monitors_) {
      if (m.primary) {
        return &m;
      }
    }
    return monitors_.empty() ? nullptr : &monitors_.front();
  }

  AspectRatio SelectedAspectRatio() const {
    return ParseAspectRatio(settings_.Get("hcu_aspect_ratio", "16:9"));
  }

  static bool WindowSizeFits(int width, int height, const MonitorInfo* monitor) {
    return !monitor || (width * monitor->scale < monitor->width && height * monitor->scale < monitor->height);
  }

  // Moves Window size to the new aspect ratio's default, unless the current size already belongs to it.
  void SelectWindowSizeForAspect() {
    const AspectRatio aspect = SelectedAspectRatio();
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    for (const WindowSize& size : kWindowSizes) {
      if (size.aspect == aspect && size.width == width && size.height == height) {
        return;
      }
    }
    const AspectRatioInfo& info = InfoFor(aspect);
    const MonitorInfo* monitor = SelectedMonitor();
    const WindowSize* chosen = nullptr;
    for (const WindowSize& size : kWindowSizes) {
      if (size.aspect != aspect || !WindowSizeFits(size.width, size.height, monitor)) {
        continue;
      }
      if (size.width == info.default_width && size.height == info.default_height) {
        chosen = &size;
        break;
      }
      if (!chosen || size.width > chosen->width) {
        chosen = &size;
      }
    }
    if (chosen) {
      settings_.SetInt("window_width", chosen->width);
      settings_.SetInt("window_height", chosen->height);
    }
  }

  // window_width/height are logical pixels; display scaling enlarges them.
  void WindowSizeCombo() {
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    const AspectRatio aspect = SelectedAspectRatio();
    const MonitorInfo* monitor = SelectedMonitor();
    const float scale = monitor ? monitor->scale : 1.0f;
    auto label = [scale](int w, int h) {
      if (w <= 0 || h <= 0) {
        return std::string("Default");
      }
      std::string text = Size(w, h);
      if (scale != 1.0f) {
        text += "   (" + Size(int(std::lround(w * scale)), int(std::lround(h * scale))) + " on screen)";
      }
      return text;
    };
    if (ImGui::BeginCombo("##window_size", label(width, height).c_str())) {
      for (const WindowSize& size : kWindowSizes) {
        if (size.aspect != aspect) {
          continue;
        }
        const bool current = size.width == width && size.height == height;
        if (!current && !WindowSizeFits(size.width, size.height, monitor)) {
          continue;
        }
        if (ImGui::Selectable(label(size.width, size.height).c_str(), current) && !current) {
          settings_.SetInt("window_width", size.width);
          settings_.SetInt("window_height", size.height);
        }
      }
      ImGui::EndCombo();
    }
  }

  void MonitorCombo() {
    const int current = settings_.GetInt("monitor", 0);
    auto label = [this](int index) {
      if (index <= 0) {
        return std::string("Default");
      }
      if (index > int(monitors_.size())) {
        return "Display " + std::to_string(index) + " (not connected)";
      }
      const MonitorInfo& m = monitors_[size_t(index - 1)];
      return "Display " + std::to_string(index) + (m.primary ? " (primary)" : "") + "   " + Size(m.width, m.height);
    };
    if (ImGui::BeginCombo("##monitor", label(current).c_str())) {
      for (int i = 1; i <= int(monitors_.size()); ++i) {
        if (ImGui::Selectable(label(i).c_str(), i == current) && i != current) {
          settings_.SetInt("monitor", i);
        }
      }
      ImGui::EndCombo();
    }
  }

  void PageGraphics(trg::Ui& ui) {
    if (!graphics_adapters_.empty()) {
      std::vector<trg::Option> adapters;
      for (const auto& [value, label] :
           GraphicsAdapterOptions(graphics_adapters_, settings_.Get("hcu_graphics_adapter", kGraphicsAdapterAuto))) {
        adapters.push_back({value, label});
      }
      ui.Combo("Graphics card",
               "Selects which GPU renders the game. Automatic uses the recommended high-performance GPU. "
               "A GPU chosen here overrides automatic selection and restarts the game window.",
               "hcu_graphics_adapter", kGraphicsAdapterAuto, adapters);
    }
    ui.Combo("Render resolution",
             "Draws the game at a multiple of its own picture, then scales it to the window. Higher is sharper and "
             "costs more GPU time. Window size is separate.",
             "resolution_scale", "1", RenderResolutionOptions(SelectedAspectRatio()));
    ui.Combo("Internal render resolution",
             "Renders the game internally at a lower resolution, then scales it to the selected render resolution. "
             "Lower values improve GPU performance but reduce image sharpness.",
             "hcu_internal_render_scale", "100",
             {{"100", "100% Native"}, {"75", "75% Performance"}, {"50", "50% Low-end"}});
    ui.Choice("Anti-aliasing", "Smooths jagged edges after the frame is drawn. Extreme is softer but cleaner.",
              "swap_post_effect", "none", {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}});
    ui.Toggle("Multisampling",
              "Use 2\xC3\x97 MSAA for best performance. Try 4\xC3\x97 only if you see graphics problems with 2\xC3\x97.",
              "hcu_msaa_compatibility", false, "2\xC3\x97 MSAA", "4\xC3\x97 MSAA (Compatibility)");
    ui.Choice("Texture filtering", "Anisotropic filtering keeps textures sharp at oblique angles. Game leaves it to the game.",
              "anisotropic_override", "3",
              {{"-1", "Game"},
               {"0", "Off"},
               {"2", "2\xC3\x97"},
               {"3", "4\xC3\x97"},
               {"4", "8\xC3\x97"},
               {"5", "16\xC3\x97"}},
              6);
  }

  static void PageGameplay(trg::Ui& ui) {
    ui.Toggle("Skip intro", "Start directly at the title screen", "hcu_skip_intro", false, "Off", "On");
    ui.Toggle("Frame rate in title", "Shows the game's frame rate in the window title: Hard Corps: Uprising | FPS: 60.00",
              "hcu_show_fps", true, "Off", "On");
    ui.Toggle("Render resolution in title", "Shows the game's render resolution in the window title",
              "hcu_show_render_resolution", false, "Off", "On");
    ui.Toggle("Frame counter", "Shows the game's frame rate in the corner. F2 toggles it while playing.",
              "hcu_show_frame_counter", false, "Hidden", "Shown");
  }

  static void PageCheats(trg::Ui& ui) {
    ui.Help("Press F10 while playing to open the Cheats menu. Changes made there apply only to the current session.");
    ui.Spacer(12);
    ui.Toggle("Infinite Health", "Prevent normal damage from reducing player health", "hcu_cheat_health", false,
              "Off", "On");
    ui.Toggle("Infinite Lives", "Prevent the number of remaining lives from decreasing", "hcu_cheat_lives", false,
              "Off", "On");
    ui.Toggle("Rapid Fire", "Fire weapons approximately twice as fast", "hcu_cheat_rapid_fire", false, "Off", "On");
    ui.Toggle("Jump Height 2x", "Reach approximately twice the normal jump height", "hcu_cheat_jump_height", false,
              "Off", "On");
    ui.Toggle("Keep Weapons on Hit", "Keep the current weapon when taking normal damage", "hcu_cheat_keep_weapons",
              false, "Off", "On");
    ui.Toggle("Movement Speed 2x", "Move at twice the normal speed", "hcu_cheat_movement_speed", false, "Off",
              "On");
    ui.Toggle("Super Armor", "Take damage normally without knockback, hit stun or knockdown", "hcu_cheat_super_armor",
              false, "Off", "On");
  }

  void PageAchievements(trg::Ui& ui) {
    if (game_files_ != GameFiles::kReady) {
      ui.Paragraph("Install the game to see its achievements.");
      return;
    }
    if (!achievements_ && !achievements_tried_) {
      achievements_tried_ = true;
      achievements_ = std::make_unique<LauncherAchievements>(paths_.game_dir, paths_.user_dir, paths_.immediate_drawer);
    }
    if (!achievements_ || !achievements_->ok()) {
      ui.Paragraph("The achievements could not be read from the game files.");
      return;
    }
    DrawAchievementSummary(ui, Summarize(achievements_->entries()));
    std::vector<trg::AchievementCard> cards;
    for (const AchievementEntry& entry : achievements_->entries()) {
      trg::AchievementCard card;
      card.icon = achievements_->Icon(entry.info);
      card.title = entry.info.label;
      card.description = EntryDescription(entry);
      card.points = static_cast<int>(entry.info.gamerscore);
      card.unlocked = entry.unlocked;
      cards.push_back(std::move(card));
    }
    ui.AchievementGrid(cards, 1);
  }

  static void DrawAchievementSummary(trg::Ui& ui, const AchievementSummary& summary) {
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::Text("%d / %d unlocked", summary.unlocked, summary.total);
    ImGui::SameLine();
    ImGui::TextColored(kAccent, "%dG / %dG", summary.earned_gamerscore, summary.total_gamerscore);
    ImGui::PopFont();
    const float fraction = summary.total > 0 ? float(summary.unlocked) / float(summary.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 6.0f * ui.scale()), "");
    ImGui::PopStyleColor();
    ui.Spacer(8);
  }

  static void PageControls(trg::Ui& ui) {
    ui.Info("Controller", "Automatic: Xbox controllers work without setup.");
    ui.Toggle("Keyboard", "Use the keyboard for gameplay controls together with the controller.", "mnk_mode", true,
              "Off", "On");
    if (ui.ConfirmRow("Keyboard defaults", "Puts the keyboard controls back to their standard layout.",
                      "Reset to defaults", "Put every key back to its default?", "Reset")) {
      ResetKeyboardDefaults(ui.settings());
      ui.SetStatus("Keyboard controls reset to defaults.");
    }
    if (ui.Section("Keyboard controls", true)) {
      ui.Help("Click a key, then press the new one. Esc cancels.");
      const bool enabled = ui.settings().GetBool("mnk_mode", true);
      if (!enabled) {
        ImGui::BeginDisabled();
      }
      for (const KeyboardAction& action : kKeyboardActions) {
        KeyboardRow(ui, action);
      }
      if (!enabled) {
        ImGui::EndDisabled();
      }
    }
  }

  // The key table holds the ReXGlue keybind_* settings, so a row edits the same store the rest of
  // the launcher uses. The store keeps the edit pending until Save or Play.
  static void KeyboardRow(trg::Ui& ui, const KeyboardAction& action) {
    const std::string shown = KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action));
    const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);
    if (!key) {
      return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const std::string binding = KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt);
    if (binding.empty()) {
      ui.SetStatus("That key cannot be used.");
      return;
    }
    ui.settings().Set(action.key, binding);
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "Hard Corps: Uprising for Xbox Live Arcade, statically recompiled to run natively on PC with the ReXGlue "
        "SDK. No game code or assets are included: the game runs from your own game files.");
    ui.Spacer(4);
    if (!paths_.game_dir.empty()) {
      ui.FolderRow("Game files", "Where the game data is read from.", "Open game folder", Utf8(paths_.game_dir));
    }
    ui.FolderRow("Saves and settings", UserDataFolderHelp(paths_.user_data_outcome), "Open data folder",
                 Utf8(paths_.user_dir));
    if (ui.ButtonRow("Settings file", "Your launcher choices, as plain text. Loaded after hard_corps_uprising.toml.",
                     "Show settings file") &&
        launcher_->Save()) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(paths_.settings_file, ec);
      trg::OpenInFileManager(Utf8(exists ? paths_.settings_file : paths_.user_dir));
    }
    ui.ResetAllRow();
    ui.Heading("Diagnostics");
    bool detailed_logging = ui.settings().GetBool("detailed_logging", true);
    ui.Row("Detailed logging", "Keeps logs for normal sessions. Crash diagnostics are always preserved.");
    if (ImGui::Checkbox("##detailed_logging", &detailed_logging)) {
      ui.settings().SetBool("detailed_logging", detailed_logging);
    }
    ui.Info("Port version", HCU_PORT_VERSION);
    ui.Info("Libraries", "ReXGlue SDK " REXGLUE_VERSION_STRING "   \xC2\xB7   TRG Launcher " HCU_TRG_VERSION);
    ui.Info("Game", "Xbox Live Arcade, title 58410A18, game version 0.0.0.3");
    ui.Help("github.com/saulob/hard-corps-uprising-recompiled");
  }

  LauncherPaths paths_;
  LauncherCallbacks cb_;
  GameSettings& settings_;
  GameFiles game_files_;
  std::vector<MonitorInfo> monitors_;
  std::vector<GraphicsAdapter> graphics_adapters_;
  std::unique_ptr<trg::Launcher> launcher_;
  ImGuiGamepad gamepad_;
  bool nav_taken_ = false;
  bool starting_ = false;
  double play_started_ = 0.0;
  ImGuiConfigFlags saved_nav_flags_ = 0;
  ImGuiBackendFlags saved_has_gamepad_ = 0;
  bool done_ = false;
  std::optional<WindowPlacement> startup_;
  PlacementTracker placement_;
  FullscreenGate fullscreen_gate_;
  LauncherFullscreenPreference fullscreen_pref_{SavedLauncherFullscreen()};
  bool restore_when_windowed_ = false;
  bool leave_requested_ = false;
  bool leave_gave_up_ = false;
  double leave_started_ = 0.0;
  std::string install_failure_;
  std::string install_failure_text_;
  std::shared_ptr<InstallOutcome> outcome_;
  trg::Task install_;
  std::vector<DlcInfo> dlc_;
  std::string dlc_failure_;
  std::string dlc_failure_text_;
  std::string dlc_removing_name_;
  std::shared_ptr<DlcOutcome> dlc_outcome_;
  trg::Task dlc_task_;
  std::unique_ptr<LauncherAchievements> achievements_;
  bool achievements_tried_ = false;
  std::optional<DlcInfo> dlc_remove_target_;
  bool dlc_remove_open_ = false;
};

}  // namespace

void LoadLauncherFonts(ImFontAtlas* atlas, float density) {
  // Latin-1 plus General Punctuation.
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0};
  ImFontConfig config;
  config.RasterizerDensity = density;
  auto load = [&](const std::filesystem::path& path) -> ImFont* {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
      return nullptr;
    }
    return atlas->AddFontFromFileTTF(Utf8(path).c_str(), g_fonts.size, &config, kRanges);
  };
  const UiFontFiles fonts = FindUiFonts();
  g_fonts.regular = load(fonts.regular);
  g_fonts.semibold = load(fonts.semibold);
  g_fonts.bold = load(fonts.bold);
}

ImFont* LauncherRegularFont() {
  return g_fonts.regular;
}

ImFont* LauncherSemiboldFont() {
  return g_fonts.semibold;
}

namespace {
bool g_skipped_once = false;
}  // namespace

bool ShouldShowLauncher(const std::filesystem::path& game_dir) {
  g_skipped_once = REXCVAR_GET(hcu_skip_launcher);
  rex::cvar::ResetToDefault("hcu_skip_launcher");
  return LauncherWanted(CheckAndLogGameFiles(game_dir) == GameFiles::kReady, g_skipped_once, ShiftHeld(),
                        REXCVAR_GET(hcu_launcher));
}

bool ShiftHeldAfterWindowOpened() {
#if defined(_WIN32)
  return false;
#else
  return !g_skipped_once && ShiftHeld();
#endif
}

namespace {

std::pair<int, int> LauncherWindowSize() {
  const MonitorInfo primary = MonitorForSetting(1);
  if (primary.width == 0 || primary.scale <= 0.0f) {
    return kLauncherWindowSize;
  }
  const int max_width = int(primary.work_width * kLauncherMaxShare / primary.scale);
  const int max_height = int(primary.work_height * kLauncherMaxShare / primary.scale);
  return {std::min(kLauncherWindowSize.first, max_width), std::min(kLauncherWindowSize.second, max_height)};
}

}  // namespace

void PrepareLauncherWindow() {
  const auto [width, height] = LauncherWindowSize();
  rex::cvar::SetFlagByName("fullscreen", "false");
  rex::cvar::SetFlagByName("window_width", std::to_string(width));
  rex::cvar::SetFlagByName("window_height", std::to_string(height));
  // Monitor 0 lets SDL centre the window on the primary display, whatever the display order.
  rex::cvar::SetFlagByName("monitor", "0");
  const WindowPlacement saved = SavedLauncherPlacement();
  InstallPreShowPlacement(PlanPreShow(saved, WorkAreas()), saved);
}

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, std::move(paths), settings, std::move(callbacks));
}

}  // namespace hcu
