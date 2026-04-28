// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_window.h"

#include <string>

#include "base/notimplemented.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "ui/base/cursor/platform_cursor.h"
#include "ui/display/screen.h"
#include "ui/display/types/display_constants.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/ozone/platform/terminal/terminal_window_manager.h"

namespace ui {

TerminalWindow::TerminalWindow(PlatformWindowDelegate* delegate,
                                       TerminalWindowManager* manager,
                                       const gfx::Rect& bounds)
    : delegate_(delegate), manager_(manager), bounds_(bounds) {
  widget_ = manager_->AddWindow(this);
  main_task_runner_ = base::SingleThreadTaskRunner::GetCurrentDefault();
  delegate->OnAcceleratedWidgetAvailable(widget_);
}

TerminalWindow::~TerminalWindow() {
  if (input_handler_) {
    input_handler_->Stop();
  }
  manager_->RemoveWindow(widget_, this);
}

void TerminalWindow::Show(bool inactive) {
  visible_ = true;
  if (!input_handler_) {
    // Capture a weak pointer and the main thread task runner. The input
    // handler callbacks run on a background thread, so we post every
    // event back to the main thread before dispatching.
    base::WeakPtr<TerminalWindow> weak_this = weak_factory_.GetWeakPtr();
    scoped_refptr<base::SingleThreadTaskRunner> task_runner = main_task_runner_;

    auto key_callback = base::BindRepeating(
        [](base::WeakPtr<TerminalWindow> self,
           scoped_refptr<base::SingleThreadTaskRunner> runner,
           KeyEvent* event) {
          KeyEvent event_copy(*event);
          runner->PostTask(
              FROM_HERE,
              base::BindOnce(
                  [](base::WeakPtr<TerminalWindow> s, KeyEvent e) {
                    if (s && s->delegate_) {
                      s->delegate_->DispatchEvent(&e);
                    }
                  },
                  self, std::move(event_copy)));
        },
        weak_this, task_runner);

    auto mouse_callback = base::BindRepeating(
        [](base::WeakPtr<TerminalWindow> self,
           scoped_refptr<base::SingleThreadTaskRunner> runner,
           MouseEvent* event) {
          MouseEvent event_copy(*event);
          runner->PostTask(
              FROM_HERE,
              base::BindOnce(
                  [](base::WeakPtr<TerminalWindow> s, MouseEvent e) {
                    if (s && s->delegate_) {
                      s->delegate_->DispatchEvent(&e);
                    }
                  },
                  self, std::move(event_copy)));
        },
        weak_this, task_runner);

    input_handler_ = std::make_unique<TerminalInputHandler>(
        std::move(key_callback), std::move(mouse_callback));
    input_handler_->Start();
  }
}

void TerminalWindow::Hide() {
  visible_ = false;
}

void TerminalWindow::Close() {
  delegate_->OnClosed();
}

bool TerminalWindow::IsVisible() const {
  return visible_;
}

void TerminalWindow::PrepareForShutdown() {}

void TerminalWindow::SetBoundsInPixels(const gfx::Rect& bounds) {
  UpdateBounds(bounds);
}

gfx::Rect TerminalWindow::GetBoundsInPixels() const {
  return bounds_;
}

void TerminalWindow::SetBoundsInDIP(const gfx::Rect& bounds) {
  SetBoundsInPixels(delegate_->ConvertRectToPixels(bounds));
}

gfx::Rect TerminalWindow::GetBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(bounds_);
}

void TerminalWindow::SetTitle(const std::u16string& title) {
  // Output the title as the terminal window title using OSC escape sequence.
  // ESC ] 2 ; <title> BEL
  fprintf(stderr, "\033]2;%s\007",
          base::UTF16ToUTF8(title).c_str());
}

void TerminalWindow::SetCapture() {}

void TerminalWindow::ReleaseCapture() {}

bool TerminalWindow::HasCapture() const {
  return false;
}

void TerminalWindow::SetFullscreen(bool fullscreen,
                                       int64_t target_display_id) {
  DCHECK_EQ(target_display_id, display::kInvalidDisplayId);
  if (!delegate_->CanFullscreen()) {
    return;
  }

  if (fullscreen) {
    if (window_state_ != PlatformWindowState::kMaximized &&
        window_state_ != PlatformWindowState::kFullScreen) {
      restored_bounds_ = bounds_;
    }
    auto zoomed_bounds =
        display::Screen::Get()->GetDisplayMatching(bounds_).work_area();
    UpdateBounds(delegate_->ConvertRectToPixels(zoomed_bounds));
    window_state_ = PlatformWindowState::kFullScreen;
    delegate_->OnWindowStateChanged(window_state_, window_state_);
  } else {
    if (window_state_ != PlatformWindowState::kFullScreen) {
      return;
    }
    if (restored_bounds_) {
      gfx::Rect restored = *restored_bounds_;
      restored_bounds_.reset();
      UpdateBounds(restored);
    }
    window_state_ = PlatformWindowState::kNormal;
    delegate_->OnWindowStateChanged(window_state_, window_state_);
  }
}

void TerminalWindow::Maximize() {
  if (!delegate_->CanMaximize()) {
    return;
  }

  if (window_state_ != PlatformWindowState::kMaximized &&
      window_state_ != PlatformWindowState::kFullScreen) {
    restored_bounds_ = bounds_;
    auto zoomed_bounds =
        display::Screen::Get()->GetDisplayMatching(bounds_).work_area();
    UpdateBounds(delegate_->ConvertRectToPixels(zoomed_bounds));
    window_state_ = PlatformWindowState::kMaximized;
    delegate_->OnWindowStateChanged(window_state_, window_state_);
  }
}

void TerminalWindow::Minimize() {
  if (window_state_ != PlatformWindowState::kMinimized) {
    if (window_state_ == PlatformWindowState::kMaximized ||
        window_state_ == PlatformWindowState::kFullScreen) {
      if (restored_bounds_) {
        gfx::Rect restored = *restored_bounds_;
        restored_bounds_.reset();
        UpdateBounds(restored);
      }
    }
    window_state_ = PlatformWindowState::kMinimized;
    delegate_->OnWindowStateChanged(window_state_, window_state_);
    Deactivate();
  }
}

void TerminalWindow::Restore() {
  if (window_state_ != PlatformWindowState::kNormal) {
    if (restored_bounds_) {
      gfx::Rect restored = *restored_bounds_;
      restored_bounds_.reset();
      UpdateBounds(restored);
    }
    window_state_ = PlatformWindowState::kNormal;
    delegate_->OnWindowStateChanged(window_state_, window_state_);
  }
}

PlatformWindowState TerminalWindow::GetPlatformWindowState() const {
  return window_state_;
}

void TerminalWindow::Activate() {
  if (activation_state_ != ActivationState::kActive) {
    activation_state_ = ActivationState::kActive;
    delegate_->OnActivationChanged(/*active=*/true);
  }
}

void TerminalWindow::Deactivate() {
  if (activation_state_ != ActivationState::kInactive) {
    activation_state_ = ActivationState::kInactive;
    delegate_->OnActivationChanged(/*active=*/false);
  }
}

void TerminalWindow::SetUseNativeFrame(bool use_native_frame) {}

bool TerminalWindow::ShouldUseNativeFrame() const {
  return false;
}

void TerminalWindow::SetCursor(scoped_refptr<PlatformCursor> cursor) {}

void TerminalWindow::MoveCursorTo(const gfx::Point& location) {}

void TerminalWindow::ConfineCursorToBounds(const gfx::Rect& bounds) {}

void TerminalWindow::SetRestoredBoundsInDIP(const gfx::Rect& bounds) {
  restored_bounds_ = delegate_->ConvertRectToPixels(bounds);
}

gfx::Rect TerminalWindow::GetRestoredBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(restored_bounds_.value_or(bounds_));
}

void TerminalWindow::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                        const gfx::ImageSkia& app_icon) {}

void TerminalWindow::SizeConstraintsChanged() {}

void TerminalWindow::UpdateBounds(const gfx::Rect& bounds) {
  bool origin_changed = bounds_.origin() != bounds.origin();
  bounds_ = bounds;
  delegate_->OnBoundsChanged({origin_changed});
}

void TerminalWindow::OnKeyEvent(KeyEvent* event) {
  if (delegate_) {
    delegate_->DispatchEvent(event);
  }
}

}  // namespace ui
