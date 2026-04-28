// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_RENDERER_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_RENDERER_H_

#include <string>

#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/terminal/terminal_constants.h"

namespace ui {

// Converts a pixel bitmap into terminal output. Supports two modes:
// - Half-block: Unicode ▀ characters with ANSI colors (2 pixels/cell).
// - Sixel: DEC Sixel graphics protocol (full pixel resolution).
class TerminalRenderer {
 public:
  // Color mode for half-block rendering.
  enum class ColorMode {
    kTrueColor,
    k256Color,
    kBasic,
  };

  TerminalRenderer();
  ~TerminalRenderer();

  TerminalRenderer(const TerminalRenderer&) = delete;
  TerminalRenderer& operator=(const TerminalRenderer&) = delete;

  void SetColorMode(ColorMode mode) { color_mode_ = mode; }
  void SetRenderMode(TerminalRenderMode mode) { render_mode_ = mode; }
  void SetTerminalSize(int cols, int rows);

  // Render a bitmap for the terminal. Output format depends on render_mode_.
  std::string RenderFrame(const SkBitmap& bitmap);

  int cols() const { return cols_; }
  int rows() const { return rows_; }

 private:
  struct RGB {
    uint8_t r, g, b;
  };

  // Half-block rendering.
  std::string RenderHalfBlock(const SkBitmap& bitmap);

  // Sixel rendering.
  std::string RenderSixel(const SkBitmap& bitmap);

  RGB SamplePixel(const SkBitmap& bitmap, int x, int y) const;
  std::string AnsiColor(RGB color, bool foreground) const;
  static int Closest256Color(RGB color);
  static int ClosestBasicColor(RGB color);

  TerminalRenderMode render_mode_ = TerminalRenderMode::kHalfBlock;
  ColorMode color_mode_ = ColorMode::kTrueColor;
  int cols_ = 120;
  int rows_ = 40;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_RENDERER_H_
