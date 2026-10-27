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
  void OnPreSetup(rex::RuntimeConfig& config) override { spyro::OnPreSetup(config); }
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
    if (spyro::PlumeActive() && window())
      spyro::plume_renderer::SetWindow(window()->GetNativeWindowHandle());
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
