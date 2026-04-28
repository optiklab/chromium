// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_window_manager.h"

#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/terminal/terminal_window.h"

namespace ui {

TerminalWindowManager::TerminalWindowManager() = default;

TerminalWindowManager::~TerminalWindowManager() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

gfx::AcceleratedWidget TerminalWindowManager::AddWindow(
    TerminalWindow* window) {
  return windows_.Add(window);
}

void TerminalWindowManager::RemoveWindow(gfx::AcceleratedWidget widget,
                                             TerminalWindow* window) {
  DCHECK_EQ(window, windows_.Lookup(widget));
  windows_.Remove(widget);
}

TerminalWindow* TerminalWindowManager::GetWindow(
    gfx::AcceleratedWidget widget) {
  return windows_.Lookup(widget);
}

gfx::AcceleratedWidget
TerminalWindowManager::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) {
  for (base::IDMap<TerminalWindow*>::const_iterator it(&windows_);
       !it.IsAtEnd(); it.Advance()) {
    const TerminalWindow* window = it.GetCurrentValue();
    gfx::Rect bounds = window->GetBoundsInPixels();
    if (bounds.Contains(point)) {
      return window->widget();
    }
  }

  return gfx::kNullAcceleratedWidget;
}

}  // namespace ui
