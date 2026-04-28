// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/ozone_platform_terminal.h"

#include <memory>

#include "base/no_destructor.h"
#include "build/build_config.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/platform/terminal/terminal_screen.h"
#include "ui/ozone/platform/terminal/terminal_surface_factory.h"
#include "ui/ozone/platform/terminal/terminal_window.h"
#include "ui/ozone/platform/terminal/terminal_window_manager.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {

namespace {

// Platform event source for the terminal platform.
class EdgeTerminalPlatformEventSource : public PlatformEventSource {
 public:
  EdgeTerminalPlatformEventSource() = default;

  EdgeTerminalPlatformEventSource(const EdgeTerminalPlatformEventSource&) =
      delete;
  EdgeTerminalPlatformEventSource& operator=(
      const EdgeTerminalPlatformEventSource&) = delete;

  ~EdgeTerminalPlatformEventSource() override = default;
};

// OzonePlatform implementation for terminal rendering.
class OzonePlatformTerminal : public OzonePlatform {
 public:
  OzonePlatformTerminal() = default;

  OzonePlatformTerminal(const OzonePlatformTerminal&) = delete;
  OzonePlatformTerminal& operator=(const OzonePlatformTerminal&) =
      delete;

  ~OzonePlatformTerminal() override = default;

  // OzonePlatform:
  SurfaceFactoryOzone* GetSurfaceFactoryOzone() override {
    return surface_factory_.get();
  }

  OverlayManagerOzone* GetOverlayManager() override {
    return overlay_manager_.get();
  }

  CursorFactory* GetCursorFactory() override { return cursor_factory_.get(); }

  InputController* GetInputController() override {
    return input_controller_.get();
  }

  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_support_host_.get();
  }

  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }

  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    return std::make_unique<TerminalWindow>(delegate,
                                                window_manager_.get(),
                                                properties.bounds);
  }

  bool IsWindowCompositingSupported() const override { return true; }

  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }

  std::unique_ptr<PlatformScreen> CreateScreen() override {
    return std::make_unique<TerminalScreen>(window_manager_.get());
  }

  void InitScreen(PlatformScreen* screen) override {}

  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* ime_key_event_dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(ime_key_event_dispatcher);
  }

  const PlatformProperties& GetPlatformProperties() override {
    static base::NoDestructor<OzonePlatform::PlatformProperties> properties;
    static bool initialized = false;
    if (!initialized) {
      properties->custom_frame_pref_default = true;
      initialized = true;
    }
    return *properties;
  }

  bool InitializeUI(const InitParams& params) override {
    window_manager_ = std::make_unique<TerminalWindowManager>();
    surface_factory_ = std::make_unique<TerminalSurfaceFactory>();

    if (!PlatformEventSource::GetInstance()) {
      platform_event_source_ =
          std::make_unique<EdgeTerminalPlatformEventSource>();
    }

    keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
        keyboard_layout_engine_.get());

    overlay_manager_ = std::make_unique<StubOverlayManager>();
    input_controller_ = std::make_unique<StubInputController>();
    cursor_factory_ = std::make_unique<BitmapCursorFactory>();
    gpu_platform_support_host_.reset(CreateStubGpuPlatformSupportHost());

    return true;
  }

  void InitializeGPU(const InitParams& params) override {
    if (!surface_factory_) {
      surface_factory_ = std::make_unique<TerminalSurfaceFactory>();
    }
  }

 private:
  std::unique_ptr<KeyboardLayoutEngine> keyboard_layout_engine_;
  std::unique_ptr<TerminalWindowManager> window_manager_;
  std::unique_ptr<TerminalSurfaceFactory> surface_factory_;
  std::unique_ptr<PlatformEventSource> platform_event_source_;
  std::unique_ptr<CursorFactory> cursor_factory_;
  std::unique_ptr<InputController> input_controller_;
  std::unique_ptr<GpuPlatformSupportHost> gpu_platform_support_host_;
  std::unique_ptr<OverlayManagerOzone> overlay_manager_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformTerminal() {
  return new OzonePlatformTerminal();
}

}  // namespace ui
