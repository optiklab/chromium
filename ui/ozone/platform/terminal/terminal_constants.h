// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_CONSTANTS_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_CONSTANTS_H_

namespace ui {

// Rendering mode for the terminal platform.
enum class TerminalRenderMode {
  // Half-block characters with ANSI colors (default, works everywhere).
  kHalfBlock,
  // Sixel graphics protocol (full pixel resolution, requires sixel-capable
  // terminal like xterm, foot, WezTerm, mlterm).
  kSixel,
};

// Returns the active render mode, parsed from --terminal-render-mode.
TerminalRenderMode TerminalGetRenderMode();

// How many browser pixels each terminal cell represents.
// Configurable at runtime via --terminal-cell-size=WxH (e.g. "8x16").
// Only used in half-block mode. In sixel mode, native pixel resolution is used.
int TerminalCellPixelWidth();
int TerminalCellPixelHeight();

// Command-line switches.
extern const char kTerminalCellSizeSwitch[];
extern const char kTerminalRenderModeSwitch[];

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_CONSTANTS_H_
