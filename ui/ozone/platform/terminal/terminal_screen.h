// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_SCREEN_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_SCREEN_H_

#include <vector>

#include "ui/display/display.h"
#include "ui/display/display_list.h"
#include "ui/gfx/geometry/point.h"
#include "ui/ozone/public/platform_screen.h"

namespace ui {

class TerminalWindowManager;

// PlatformScreen implementation for the terminal platform.
// Provides a single virtual display whose size is derived from the terminal
// dimensions (columns * cell_width, rows * cell_height).
class TerminalScreen : public PlatformScreen {
 public:
  explicit TerminalScreen(TerminalWindowManager* window_manager);

  TerminalScreen(const TerminalScreen&) = delete;
  TerminalScreen& operator=(const TerminalScreen&) = delete;

  ~TerminalScreen() override;

  // PlatformScreen:
  const std::vector<display::Display>& GetAllDisplays() const override;
  display::Display GetPrimaryDisplay() const override;
  display::Display GetDisplayForAcceleratedWidget(
      gfx::AcceleratedWidget widget) const override;
  gfx::Point GetCursorScreenPoint() const override;
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayNearestPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayMatching(
      const gfx::Rect& match_rect) const override;
  bool IsScreenSaverActive() const override;
  base::TimeDelta CalculateIdleTime() const override;
  void AddObserver(display::DisplayObserver* observer) override;
  void RemoveObserver(display::DisplayObserver* observer) override;

 private:
  void CreateDisplay();

  raw_ptr<TerminalWindowManager> window_manager_;
  display::DisplayList display_list_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_SCREEN_H_
