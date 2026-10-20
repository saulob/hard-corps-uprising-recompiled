#include "window_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "aspect_ratio.h"
#include "cursor_input.h"
#include "input_activity.h"
#include "settings.h"
#include "window_title.h"

namespace {

constexpr double kSampleSeconds = 1.0;
// Above the app's own input listener (z-order 0), so F11 never reaches the game.
constexpr size_t kWindowHostZOrder = 1000;

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_active{false};
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
std::function<bool()> g_ui_wants_mouse;
// UI thread only.
hcu::CursorHider g_cursor;
// Input polling thread only.
std::unordered_map<uint64_t, hcu::ControllerActivityFilter> g_controller_filters;
std::optional<double> g_title_fps;
std::string g_shown_title;

std::mutex g_sample_mutex;
std::optional<Clock::time_point> g_sample_start;
uint64_t g_sample_frames = 0;
uint64_t g_total_frames = 0;
GuestFrameStats g_stats;

std::optional<std::string> ShownRenderResolution() {
  if (!REXCVAR_GET(hcu_show_render_resolution)) {
    return std::nullopt;
  }
  const int scale = std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()));
  return hcu::RenderResolutionText(scale, hcu::ParseAspectRatio(rex::cvar::GetFlagByName("hcu_aspect_ratio")));
}

// Composes all title parts together, so one update never drops another.
void RefreshTitle() {
  const std::optional<double> fps = REXCVAR_GET(hcu_show_fps) ? g_title_fps : std::nullopt;
  std::string title = hcu::WindowTitleText(ShownRenderResolution(), fps);
  if (title != g_shown_title) {
    g_shown_title = title;
    g_window->SetTitle(title);
  }
}

void ShowFps(double fps) {
  if (g_active.load(std::memory_order_relaxed)) {
    g_title_fps = fps;
    RefreshTitle();
  }
}

void CountGuestFrame() {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard lock(g_sample_mutex);
  const auto now = Clock::now();
  if (!g_sample_start) {
    g_sample_start = now;
    return;
  }
  ++g_sample_frames;
  ++g_total_frames;
  const double elapsed = std::chrono::duration<double>(now - *g_sample_start).count();
  if (elapsed < kSampleSeconds) {
    return;
  }
  const double fps = g_sample_frames / elapsed;
  g_stats = {fps, elapsed * 1000.0 / g_sample_frames, g_total_frames};
  g_sample_start = now;
  g_sample_frames = 0;
  g_app_context->CallInUIThread([fps] { ShowFps(fps); });
}

bool UiWantsMouse() { return g_ui_wants_mouse && g_ui_wants_mouse(); }

void UpdateCursor() {
  if (!g_window || !g_active.load(std::memory_order_relaxed)) {
    return;
  }
  const bool visible = g_cursor.visible() || UiWantsMouse();
  const auto wanted = visible ? rex::ui::Window::CursorVisibility::kVisible
                              : rex::ui::Window::CursorVisibility::kHidden;
  if (g_window->GetCursorVisibility() != wanted) {
    g_window->SetCursorVisibility(wanted);
  }
}

void RestoreCursor() {
  if (g_window && g_active.load(std::memory_order_relaxed)) {
    g_window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
  }
}

void OnControllerActivity(rex::input::DeviceId device,
                          const rex::input::X_INPUT_GAMEPAD& gamepad) {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  const uint64_t id = static_cast<uint64_t>(device);
  if (!g_controller_filters[id].Update(gamepad)) {
    return;
  }
  if (g_app_context) {
    g_app_context->CallInUIThread([] {
      if (!g_active.load(std::memory_order_relaxed)) {
        return;
      }
      g_cursor.ControllerActivity();
      UpdateCursor();
    });
  }
}

class WindowHost final : public rex::ui::WindowListener, public rex::ui::WindowInputListener {
 public:
  void OnClosing(rex::ui::UIEvent&) override {
    rex::input::SetInputActivityCallback(nullptr);
    RestoreCursor();
    g_active.store(false, std::memory_order_relaxed);
  }

  void OnGotFocus(rex::ui::UISetupEvent&) override {
    g_cursor.SetFocused(true);
    UpdateCursor();
  }

  void OnLostFocus(rex::ui::UISetupEvent&) override {
    g_cursor.SetFocused(false);
    UpdateCursor();
  }

  void OnMouseMove(rex::ui::MouseEvent& e) override { NoteMouse(e); }
  void OnMouseDown(rex::ui::MouseEvent& e) override { NoteMouse(e); }
  void OnMouseWheel(rex::ui::MouseEvent& e) override { NoteMouse(e); }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.target() != g_window || !g_window->HasFocus()) {
      return;
    }
    if (e.virtual_key() == rex::ui::VirtualKey::kF11) {
      e.set_handled(true);
      if (e.prev_state()) {
        return;
      }
      // SetFullscreen(true) is SDL's default fullscreen mode, which is borderless desktop.
      const bool fullscreen = !g_window->IsFullscreen();
      g_window->SetFullscreen(fullscreen);
      REXLOG_INFO("Window mode: {}", fullscreen ? "borderless fullscreen" : "windowed");
      UpdateCursor();
      return;
    }
    if (e.prev_state() || g_cursor.overlay_open()) {
      return;
    }
    g_cursor.KeyboardActivity();
    UpdateCursor();
  }

 private:
  static void NoteMouse(rex::ui::MouseEvent& e) {
    if (e.target() != g_window || !g_window->HasFocus()) {
      return;
    }
    if (!g_cursor.overlay_open() && !UiWantsMouse()) {
      g_cursor.MouseActivity();
    }
    UpdateCursor();
  }
};

WindowHost window_host;

// Runtime overlays are not ModalDialog, so ImGui's mouse capture is checked each frame.
class CursorSync final : public rex::ui::ImGuiDialog {
 public:
  explicit CursorSync(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO&) override { UpdateCursor(); }
};

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<bool()> ui_wants_mouse) {
  g_window = window;
  g_app_context = &app_context;
  g_ui_wants_mouse = std::move(ui_wants_mouse);
  g_cursor = hcu::CursorHider{};
  g_cursor.SetFocused(window->HasFocus());
  g_controller_filters.clear();
  RefreshTitle();
  g_active.store(true, std::memory_order_relaxed);
  window->AddListener(&window_host);
  window->AddInputListener(&window_host, kWindowHostZOrder);
  rex::input::SetInputActivityCallback(OnControllerActivity);
  UpdateCursor();
}

void SetCursorOverlayOpen(bool open) {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  g_cursor.SetOverlayOpen(open);
  UpdateCursor();
}

void CreateCursorSync(rex::ui::ImGuiDrawer* drawer) {
  new CursorSync(drawer);  // retained by the drawer for the session
}

// The guest can end without the window receiving OnClosing.
void DetachWindowHost() {
  rex::input::SetInputActivityCallback(nullptr);
  RestoreCursor();
  g_active.store(false, std::memory_order_relaxed);
  g_controller_filters.clear();
  g_ui_wants_mouse = nullptr;
  g_app_context = nullptr;
  g_window = nullptr;
}

GuestFrameStats GetGuestFrameStats() {
  std::lock_guard lock(g_sample_mutex);
  return g_stats;
}

REX_EXTERN(__imp__sub_825520F8);

// Swap routine. VdSwap is gated by obj+21748, so calls can exceed presented frames.
REX_HOOK_RAW(sub_825520F8) {
  __imp__sub_825520F8(ctx, base);
  CountGuestFrame();
}
