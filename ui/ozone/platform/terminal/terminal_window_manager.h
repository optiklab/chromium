// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_WINDOW_MANAGER_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_WINDOW_MANAGER_H_

#include <stdint.h>

#include "base/containers/id_map.h"
#include "base/threading/thread_checker.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_ui_types.h"

namespace ui {

class TerminalWindow;

// Manages terminal windows (maps accelerated widgets to window objects).
class TerminalWindowManager {
 public:
  TerminalWindowManager();

  TerminalWindowManager(const TerminalWindowManager&) = delete;
  TerminalWindowManager& operator=(const TerminalWindowManager&) =
      delete;

  ~TerminalWindowManager();

  // Register a new window. Returns the window id.
  gfx::AcceleratedWidget AddWindow(TerminalWindow* window);

  // Remove a window.
  void RemoveWindow(gfx::AcceleratedWidget widget,
                    TerminalWindow* window);

  // Find a window object by id.
  TerminalWindow* GetWindow(gfx::AcceleratedWidget widget);

  // Return an accelerated widget at screen point.
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point);

 private:
  base::IDMap<TerminalWindow*> windows_;
  base::ThreadChecker thread_checker_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_WINDOW_MANAGER_H_
