// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_constants.h"

#include <cstdlib>
#include <string>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"

namespace ui {

const char kTerminalCellSizeSwitch[] = "terminal-cell-size";
const char kTerminalRenderModeSwitch[] = "terminal-render-mode";

namespace {

constexpr int kDefaultCellPixelWidth = 4;
constexpr int kDefaultCellPixelHeight = 8;

struct CellSize {
  int width = kDefaultCellPixelWidth;
  int height = kDefaultCellPixelHeight;
};

CellSize ParseCellSize() {
  CellSize size;
  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kTerminalCellSizeSwitch)) {
    return size;
  }

  std::string value = command_line->GetSwitchValueASCII(kTerminalCellSizeSwitch);
  // Expected format: WxH, e.g. "8x16"
  size_t x_pos = value.find('x');
  if (x_pos == std::string::npos) {
    x_pos = value.find('X');
  }
  if (x_pos == std::string::npos) {
    LOG(WARNING) << "Invalid --terminal-cell-size format: " << value
                 << " (expected WxH, e.g. 8x16)";
    return size;
  }

  int w = 0;
  int h = 0;
  if (base::StringToInt(value.substr(0, x_pos), &w) &&
      base::StringToInt(value.substr(x_pos + 1), &h) && w > 0 && h > 0) {
    size.width = w;
    size.height = h;
    LOG(INFO) << "Terminal cell size: " << w << "x" << h;
  } else {
    LOG(WARNING) << "Invalid --terminal-cell-size values: " << value;
  }

  return size;
}

const CellSize& GetCellSize() {
  static const CellSize size = ParseCellSize();
  return size;
}

TerminalRenderMode ParseRenderMode() {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kTerminalRenderModeSwitch)) {
    return TerminalRenderMode::kHalfBlock;
  }

  std::string value =
      command_line->GetSwitchValueASCII(kTerminalRenderModeSwitch);
  if (value == "sixel") {
    LOG(INFO) << "Terminal render mode: sixel";
    return TerminalRenderMode::kSixel;
  }
  if (value == "halfblock") {
    return TerminalRenderMode::kHalfBlock;
  }

  LOG(WARNING) << "Unknown --terminal-render-mode: " << value
               << " (valid: halfblock, sixel). Defaulting to halfblock.";
  return TerminalRenderMode::kHalfBlock;
}

}  // namespace

TerminalRenderMode TerminalGetRenderMode() {
  static const TerminalRenderMode mode = ParseRenderMode();
  return mode;
}

int TerminalCellPixelWidth() {
  return GetCellSize().width;
}

int TerminalCellPixelHeight() {
  return GetCellSize().height;
}

}  // namespace ui
