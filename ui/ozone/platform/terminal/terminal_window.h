// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_WINDOW_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_WINDOW_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/platform/terminal/terminal_input_handler.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

class TerminalWindowManager;

// PlatformWindow implementation for the terminal Ozone platform.
// Manages a virtual window whose bounds are derived from terminal dimensions.
class TerminalWindow : public PlatformWindow {
 public:
  TerminalWindow(PlatformWindowDelegate* delegate,
                     TerminalWindowManager* manager,
                     const gfx::Rect& bounds);

  TerminalWindow(const TerminalWindow&) = delete;
  TerminalWindow& operator=(const TerminalWindow&) = delete;

  ~TerminalWindow() override;

  // PlatformWindow:
  void Show(bool inactive) override;
  void Hide() override;
  void Close() override;
  bool IsVisible() const override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInDIP() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override;
  bool HasCapture() const override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  PlatformWindowState GetPlatformWindowState() const override;
  void Activate() override;
  void Deactivate() override;
  void SetUseNativeFrame(bool use_native_frame) override;
  bool ShouldUseNativeFrame() const override;
  void SetCursor(scoped_refptr<PlatformCursor> cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  void SetRestoredBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetRestoredBoundsInDIP() const override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void SizeConstraintsChanged() override;

  gfx::AcceleratedWidget widget() const { return widget_; }

 private:
  void UpdateBounds(const gfx::Rect& bounds);
  void OnKeyEvent(KeyEvent* event);

  enum class ActivationState {
    kUnknown,
    kActive,
    kInactive,
  };

  raw_ptr<PlatformWindowDelegate> delegate_ = nullptr;
  raw_ptr<TerminalWindowManager> manager_;
  gfx::Rect bounds_;

  gfx::AcceleratedWidget widget_;

  bool visible_ = false;
  std::optional<gfx::Rect> restored_bounds_;
  PlatformWindowState window_state_ = PlatformWindowState::kUnknown;
  ActivationState activation_state_ = ActivationState::kUnknown;

  std::unique_ptr<TerminalInputHandler> input_handler_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  base::WeakPtrFactory<TerminalWindow> weak_factory_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_WINDOW_H_
