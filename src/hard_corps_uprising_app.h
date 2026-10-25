#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/version.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/flags.h>
#include <rex/ui/windowed_app_context.h>

#include "achievements_overlay.h"
#include "active_log.h"
#include "aspect_ratio.h"
#include "cheat_menu.h"
#include "diagnostics.h"
#include "frame_counter.h"
#include "game_files.h"
#include "guest_functions.h"
#include "input_summary.h"
#include "internal_render_scale.h"
#include "launcher.h"
#include "live_fix.h"
#include "local_stats.h"
#include "log_filter.h"
#include "log_path.h"
#include "modal_dialog.h"
#include "notice_dialog.h"
#include "platform.h"
#include "port_version.h"
#include "session_log.h"
#include "settings.h"
#include "system_info.h"
#include "title_start.h"
#include "trg/game_helpers.h"
#include "user_data.h"
#include "window_host.h"

class HardCorpsUprisingApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    hcu::SetApplicationId();
    hcu::InstallGuestFunctionTable(PPCImageConfig.func_mappings);
    return std::unique_ptr<HardCorpsUprisingApp>(
        new HardCorpsUprisingApp(ctx, "hard_corps_uprising", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    shipped_config_ = paths.config_path;
    const std::filesystem::path port_folder = hcu::PortFolder();
    if (paths.game_data_root.empty()) {
      paths.game_data_root = port_folder / "game";
    }
    ChooseUserData(paths, port_folder);
    if (const auto inherited =
            hcu::TakeInheritedSessionLog({port_folder / "logs", paths.user_data_root / "logs"})) {
      rex::cvar::SetFlagFromCommandLine("log_file", hcu::Utf8(*inherited));
      hcu::InstallCrashReports();
    }
    // ReXGlue writes its logs beside the executable, which is read-only inside an AppImage.
    const bool appimage = port_folder != rex::filesystem::GetExecutableFolder();
    if (appimage && rex::cvar::GetFlagByName("log_file").empty()) {
      const std::filesystem::path logs = hcu::LogsFolder(port_folder, paths.user_data_root, {}, appimage);
      if (const std::filesystem::path file = hcu::NextLogFile(logs, "hard_corps_uprising"); !file.empty()) {
        rex::cvar::SetFlagByName("log_file", hcu::Utf8(file));
      }
    }
  }

  bool SetupPresentation() override {
    if (rex::ReXApp::SetupPresentation()) {
      return true;
    }
    REXLOG_WARN("Graphics unavailable; opening the launcher without graphics");
    hcu::RunLauncherWithoutGraphics(DescribeGameFiles());
    return false;
  }

  void OnPostInitLogging() override {
    hcu::InitializeProcessUptime();
    hcu::InstallCrashReports();
    const hcu::UserDataMessage message = hcu::DescribeUserData(user_data_);
    if (message.warning) {
      REXLOG_WARN("{}", message.text);
    } else {
      REXLOG_INFO("{}", message.text);
    }
    hcu::LoadPlayerSettings(hcu::PlayerSettingsPath(user_data_root()));
    hcu::ApplyDetailedLogging();
    hcu::InstallSessionLogCleanup();
    const hcu::ApplicationIdentity identity{
        .product = "Hard Corps: Uprising",
        .version = hcu::PortVersion(),
        .build_config = hcu::BuildConfigurationName(),
        .architecture = hcu::ArchitectureName(),
        .sdk = REXGLUE_VERSION_STRING,
        .backend = hcu::GraphicsBackendName(),
    };
    REXLOG_INFO("{}", hcu::FormatApplicationVersion(identity));
    REXLOG_INFO("{}", hcu::FormatBuildIdentity(identity));
    REXLOG_INFO("{}", hcu::GameFilesLogText(hcu::CheckGameFiles(game_data_root())));
    hcu::ApplyGraphicsAdapter();
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    hcu::LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnDpiScaleChanged(float scale) override {
    hcu::UpdateLauncherFonts(imgui_drawer(), immediate_drawer(), scale);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    game_settings_.Capture(hcu::PlayerSettingsPath(user_data_root()), shipped_config_);
    show_launcher_ = hcu::ShouldShowLauncher(game_data_root());
    if (show_launcher_) {
      hcu::PrepareLauncherWindow();
    }
    game_settings_.MarkWindowCreated();
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (window()) {
      window()->SetTitle(kWindowTitle);
    }
    hcu::ApplyWindowIcon();
    game_settings_.CaptureLateRegistered();
    if (!show_launcher_ && hcu::ShiftHeldAfterWindowOpened()) {
      show_launcher_ = true;
    }
    if (!show_launcher_) {
      hcu::ApplyMultisampling();
      hcu::ApplyPlayerNames();
      hcu::ApplyAspectRatio();
      hcu::ApplyInternalRenderPath();
      return defaults;
    }
    if (!imgui_drawer()) {
      game_settings_.Apply();
      return defaults;
    }
    REXLOG_INFO("Launcher shown");
    hcu::LauncherCallbacks callbacks;
    callbacks.play = [this, resume, defaults] {
      app_context().CallInUIThreadDeferred([this, resume, defaults] {
        game_settings_.Apply();
        hcu::ApplyDetailedLogging();
        resume(defaults);
      });
    };
    callbacks.window_changed = [this] { return game_settings_.WindowChanged(); };
    callbacks.restart = [this] {
      if (!hcu::RelaunchSelf(L"--hcu_skip_launcher=true")) {
        return false;
      }
      app_context().CallInUIThreadDeferred([] {
        REXLOG_INFO("Restarted in a new window; hard-exiting this process.");
        rex::FlushLogging();
        std::_Exit(0);
      });
      return true;
    };
    callbacks.quit = [this] { app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); }); };
    callbacks.window_handle = [this] { return window() ? window()->GetNativeWindowHandle() : nullptr; };
    callbacks.window_fullscreen = [this] { return window() && window()->IsFullscreen(); };
    callbacks.set_window_fullscreen = [this](bool fullscreen) {
      if (window()) {
        window()->SetFullscreen(fullscreen);
      }
    };
    const std::filesystem::path port_folder = hcu::PortFolder();
    const std::filesystem::path logs_dir =
        hcu::LogsFolder(port_folder, defaults.user_data_root, hcu::ActiveLogFile(),
                        port_folder != rex::filesystem::GetExecutableFolder());
    hcu::ShowLauncher(imgui_drawer(),
                      {defaults.game_data_root, defaults.user_data_root,
                       hcu::PlayerSettingsPath(defaults.user_data_root), logs_dir, user_data_.outcome,
                       immediate_drawer()},
                      game_settings_, std::move(callbacks));
    return std::nullopt;
  }

  void LaunchModule() override {
    const int monitor = game_settings_.GetInt("monitor", std::atoi(rex::cvar::GetFlagByName("monitor").c_str()));
    hcu::StartupWindowInfo startup_window;
    startup_window.fullscreen = window() != nullptr && window()->IsFullscreen();
    startup_window.selected_width = game_settings_.GetInt(
        "window_width", std::atoi(rex::cvar::GetFlagByName("window_width").c_str()));
    startup_window.selected_height = game_settings_.GetInt(
        "window_height", std::atoi(rex::cvar::GetFlagByName("window_height").c_str()));
    startup_window.configured_width = window() != nullptr ? int(window()->GetDesiredLogicalWidth())
                                                          : startup_window.selected_width;
    startup_window.configured_height = window() != nullptr ? int(window()->GetDesiredLogicalHeight())
                                                           : startup_window.selected_height;
    const hcu::WindowSize drawable = CurrentDrawableSize(window());
    startup_window.output_width = drawable.width;
    startup_window.output_height = drawable.height;
    startup_window.controller = hcu::ControllerSummary();
    hcu::LogStartupDiagnostics(game_settings_, hcu::MonitorForSetting(monitor), monitor, game_data_root(),
                               startup_window);
    ReXApp::LaunchModule();
  }

  void OnPostSetup() override {
    REXLOG_INFO("{}", hcu::DescribeInternalRenderResolution(
                          std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str())),
                          std::atoi(rex::cvar::GetFlagByName("hcu_internal_render_scale").c_str()),
                          hcu::GuestWidth(hcu::ParseAspectRatio(rex::cvar::GetFlagByName("hcu_aspect_ratio")))));
#if defined(_WIN32)
    REXLOG_INFO("Scheduling: {}", trg::TuneProcessScheduling());
#endif
    auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
    hcu::SetTitleStartInputSystem(input);
    AttachWindowHost(window(), app_context(), [this] {
      rex::ui::ImGuiDrawer* drawer = imgui_drawer();
      return drawer && drawer->GetIO().WantCaptureMouse;
    });
    if (window()) {
      hcu::CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
    }
    if (window() && imgui_drawer()) {
      cheat_menu_.Attach(window(), imgui_drawer());
      hcu::CreateFrameCounter(imgui_drawer());
      CreateCursorSync(imgui_drawer());
      // Replaces ReXApp's own callback, which the menu's version repeats.
      if (input) {
        input->SetActiveCallback([this] { return cheat_menu_.GameInputActive(); });
      }
      hcu::ModalDialog::AttachEscapeKey(window());
    }
    InstallLiveServices();
  }

  void OnShutdown() override {
    // ReXApp destroys the window before the runtime; the input drivers must detach while it still exists.
    hcu::SetTitleStartInputSystem(nullptr);
    if (auto* input = runtime() ? runtime()->input_system() : nullptr) {
      input->Shutdown();
    }
    hcu::ModalDialog::DetachEscapeKey();
    delete achievements_;
    delete notice_;
    cheat_menu_.Detach();
    DetachWindowHost();
    REXLOG_INFO("{}", hcu::FormatCleanExit(hcu::ProcessUptimeText()));
  }

  // Window close is a hard exit: the base TerminateTitle would call StopTitleThreads and wait on guest
  // threads that are still running guest code. The OS reclaims the process.
  void OnClosing(rex::ui::UIEvent&) override {
    REXLOG_INFO("Window closing, hard-exiting process.");
    hcu::PrepareHardExit();
    std::_Exit(0);
  }

  // F7 toggles the same overlay as the game's Achievements item. The overlay is owned here, not by ReXApp.
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    if (achievements_) {
      achievements_->RequestClose();
    } else {
      OpenAchievements();
    }
    return nullptr;
  }

 private:
  hcu::GraphicsFallbackInfo DescribeGameFiles() const {
    switch (hcu::CheckGameFiles(game_data_root()).state) {
      case hcu::GameFiles::kReady:
        return {hcu::Utf8(game_data_root()), "Ready", true};
      case hcu::GameFiles::kUnsupported:
        return {hcu::Utf8(game_data_root()), "Unsupported version", false};
      default:
        return {hcu::Utf8(game_data_root()), "Setup required", false};
    }
  }

  // The cache folder ReXGlue derived from the system root follows the user data root unless it was set explicitly.
  void ChooseUserData(rex::PathConfig& paths, const std::filesystem::path& port_folder) {
    user_data_ = hcu::SelectUserData({port_folder, paths.user_data_root, !REXCVAR_GET(user_data_root).empty()});
    if (user_data_.root == paths.user_data_root) {
      return;
    }
    paths.user_data_root = user_data_.root;
    if (REXCVAR_GET(cache_root).empty()) {
      paths.cache_root = user_data_.root / "cache";
    }
  }

  void InstallLiveServices() {
    auto* kernel = runtime()->kernel_state();
    const auto* profile = kernel ? kernel->user_profile() : nullptr;
    if (!profile) {
      return;
    }
    hcu::live::Install({
        .stats_file = hcu::StatsFilePath(std::filesystem::absolute(user_data_root()), kernel->title_id()),
        .user = {profile->xuid(), profile->name()},
        .bridge =
            {.open_achievements = [this] { app_context().CallInUIThreadDeferred([this] { OpenAchievements(); }); },
             .open_download_content =
                 [this] { app_context().CallInUIThreadDeferred([this] { OpenDownloadContent(); }); }},
        .kernel = kernel,
    });
  }

  void OpenAchievements() {
    if (achievements_ || hcu::ModalDialog::AnyOpen() || !runtime() || !runtime()->kernel_state() || !imgui_drawer() ||
        !immediate_drawer()) {
      return;
    }
    achievements_ = new hcu::AchievementsOverlay(imgui_drawer(), immediate_drawer(), runtime(), &achievements(),
                                                 [this] { achievements_ = nullptr; });
  }

  void OpenDownloadContent() {
    if (notice_ || hcu::ModalDialog::AnyOpen() || !imgui_drawer()) {
      return;
    }
    notice_ = new hcu::NoticeDialog(
        imgui_drawer(), "DOWNLOAD CONTENT",
        {"The Xbox Live Marketplace is not part of the PC version.",
         "Additional characters are installed from the PC launcher: start the game again (hold Shift if the "
         "launcher does not open) and use its DLC page."},
        [this] { notice_ = nullptr; });
  }

  hcu::GameSettings game_settings_;
  hcu::CheatMenu cheat_menu_;
  hcu::AchievementsOverlay* achievements_ = nullptr;
  hcu::NoticeDialog* notice_ = nullptr;
  std::filesystem::path shipped_config_;
  hcu::UserDataSelection user_data_;
  bool show_launcher_ = false;
};
