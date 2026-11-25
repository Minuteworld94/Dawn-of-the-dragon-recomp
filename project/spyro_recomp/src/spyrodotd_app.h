// spyrodotd - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>

#include <filesystem>
#include <fstream>

#include <rex/ui/window.h>

#include "plume_renderer.h"
#include "spyro_controls.h"
#include "spyro_platform.h"
#include "spyro_settings.h"

class SpyroDotdApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SpyroDotdApp>(new SpyroDotdApp(ctx, "spyrodotd",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // Recomp Settings: load recomp_settings.ini, window mode, and the chosen GPU backend.
  void OnPreSetup(rex::RuntimeConfig& config) override {
#if !defined(_WIN32)
    spyro::platform::InstallCrashReport();  // Linux: abort() crashes print their stack (WORK_LOG 161)
#endif
    spyro::OnPreSetup(config);
    spyro::controls::Load();  // controls.ini (Customize controls, WORK_LOG 150)
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // Analysis aid (env SPYRO_IMAGEDUMP=1): write the decrypted, relocated guest image (0x82000000,
  // 0x8E0000 bytes) to image_dump.bin in the working directory once, so offline scripts can scan
  // it for function pointers / vtables the code analysis cannot see.
  void OnPostLoadXexImage() override {
    constexpr uint32_t kImageBase = 0x82000000u;
    constexpr size_t kImageSize = 0x8E0000;
    if (!std::getenv("SPYRO_IMAGEDUMP") || std::filesystem::exists("image_dump.bin")) return;
    const auto* guest_base = runtime()->virtual_membase();
    if (!guest_base) return;
    std::ofstream out("image_dump.bin", std::ios::binary);
    out.write(reinterpret_cast<const char*>(guest_base + kImageBase), kImageSize);
  }
  // plume renderer: give it the game window (reXglue opened it in SetupPresentation).
  void OnPostSetup() override {
    // The native window (HWND on Windows, the SDL_Window on Linux - WORK_LOG 156) for keyboard focus
    // and keys (spyro_platform) and for plume to draw into.
    if (window()) spyro::platform::SetGameWindow(window()->GetNativeWindowHandle());
    if (spyro::PlumeActive() && window())
      spyro::plume_renderer::SetWindow(window()->GetNativeWindowHandle());
    // Keyboard and mouse controls: mouse motion and the wheel come from the window (WORK_LOG 150).
    // The screens' parchment is read from the game's files (LVL000.KXE, WORK_LOG 154).
    spyro::controls::Attach(window(), game_data_root());
    setup_done_ = true;  // OnPostSetup is the last step of a successful start
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // The first step of reXglue's teardown (after quitting, or after a failed start).
  void OnShutdown() override {
#if !defined(_WIN32)
    // Linux: end here. The rest of the teardown crashed on the Steam Deck after a failed start
    // (glibc "pthread_mutex_lock ... e != ESRCH || !robust", WORK_LOG 161); everything of ours is
    // saved already. Exit code 1 = the game never got going (the launcher then shows a message).
    spyro::platform::EndProcess(setup_done_ ? 0 : 1);
#endif
  }
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  bool setup_done_ = false;
};
