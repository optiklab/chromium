// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_screen.h"

#include "base/time/time.h"
#include "ui/display/display.h"
#include "ui/display/display_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/ozone/platform/terminal/terminal_constants.h"
#include "ui/ozone/platform/terminal/terminal_window.h"
#include "ui/ozone/platform/terminal/terminal_window_manager.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace ui {

namespace {

gfx::Size GetTerminalPixelSize() {
  int cols = 120;
  int rows = 40;
  int xpixel = 0;
  int ypixel = 0;

#if BUILDFLAG(IS_WIN)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    cols = ws.ws_col;
    rows = ws.ws_row;
    xpixel = ws.ws_xpixel;
    ypixel = ws.ws_ypixel;
  }
#endif

  // In sixel mode, use the terminal's native pixel resolution if available.
  if (TerminalGetRenderMode() == TerminalRenderMode::kSixel) {
    if (xpixel > 0 && ypixel > 0) {
      return gfx::Size(xpixel, ypixel);
    }
    // Fallback: assume typical cell size of 8×16 pixels.
    return gfx::Size(cols * 8, rows * 16);
  }

  // Half-block mode: use configurable cell-to-pixel mapping.
  return gfx::Size(cols * TerminalCellPixelWidth(),
                   rows * TerminalCellPixelHeight());
}

}  // namespace

TerminalScreen::TerminalScreen(
    TerminalWindowManager* window_manager)
    : window_manager_(window_manager) {
  CreateDisplay();
}

TerminalScreen::~TerminalScreen() = default;

void TerminalScreen::CreateDisplay() {
  gfx::Size size = GetTerminalPixelSize();
  display::Display display(/*id=*/1);
  display.set_bounds(gfx::Rect(size));
  display.set_work_area(gfx::Rect(size));
  display.set_device_scale_factor(1.0f);
  display_list_.AddDisplay(display, display::DisplayList::Type::PRIMARY);
}

const std::vector<display::Display>& TerminalScreen::GetAllDisplays()
    const {
  return display_list_.displays();
}

display::Display TerminalScreen::GetPrimaryDisplay() const {
  auto iter = display_list_.GetPrimaryDisplayIterator();
  if (iter == display_list_.displays().end()) {
    return display::Display::GetDefaultDisplay();
  }
  return *iter;
}

display::Display TerminalScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  return GetPrimaryDisplay();
}

gfx::Point TerminalScreen::GetCursorScreenPoint() const {
  return gfx::Point();
}

gfx::AcceleratedWidget
TerminalScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  return window_manager_->GetAcceleratedWidgetAtScreenPoint(point);
}

display::Display TerminalScreen::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  return GetPrimaryDisplay();
}

display::Display TerminalScreen::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  return GetPrimaryDisplay();
}

bool TerminalScreen::IsScreenSaverActive() const {
  return false;
}

base::TimeDelta TerminalScreen::CalculateIdleTime() const {
  return base::TimeDelta();
}

void TerminalScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void TerminalScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

}  // namespace ui
