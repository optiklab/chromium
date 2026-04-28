// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "base/strings/stringprintf.h"
#include "third_party/skia/include/core/SkColor.h"

namespace ui {

namespace {

// The Unicode upper-half-block character. When printed, the foreground color
// fills the top half of the cell and the background color fills the bottom
// half, effectively giving us two vertical pixels per cell.
constexpr char kUpperHalfBlock[] = "\xe2\x96\x80";  // U+2580 ▀

// ANSI escape to move cursor to top-left.
constexpr char kCursorHome[] = "\033[H";

// ANSI escape to reset all attributes.
constexpr char kReset[] = "\033[0m";

// ANSI escape to hide the cursor during rendering.
constexpr char kHideCursor[] = "\033[?25l";

// ANSI escape to show the cursor.
constexpr char kShowCursor[] = "\033[?25h";

// Basic 16 ANSI colors (approximate RGB values).
constexpr struct {
  uint8_t r, g, b;
  int fg_code;
  int bg_code;
} kBasicColors[] = {
    {0, 0, 0, 30, 40},         // Black
    {170, 0, 0, 31, 41},       // Red
    {0, 170, 0, 32, 42},       // Green
    {170, 85, 0, 33, 43},      // Yellow/Brown
    {0, 0, 170, 34, 44},       // Blue
    {170, 0, 170, 35, 45},     // Magenta
    {0, 170, 170, 36, 46},     // Cyan
    {170, 170, 170, 37, 47},   // White
    {85, 85, 85, 90, 100},     // Bright Black
    {255, 85, 85, 91, 101},    // Bright Red
    {85, 255, 85, 92, 102},    // Bright Green
    {255, 255, 85, 93, 103},   // Bright Yellow
    {85, 85, 255, 94, 104},    // Bright Blue
    {255, 85, 255, 95, 105},   // Bright Magenta
    {85, 255, 255, 96, 106},   // Bright Cyan
    {255, 255, 255, 97, 107},  // Bright White
};

int ColorDistanceSq(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2,
                    uint8_t g2, uint8_t b2) {
  int dr = static_cast<int>(r1) - r2;
  int dg = static_cast<int>(g1) - g2;
  int db = static_cast<int>(b1) - b2;
  // Weighted distance (human perception favors green).
  return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}

}  // namespace

TerminalRenderer::TerminalRenderer() = default;
TerminalRenderer::~TerminalRenderer() = default;

void TerminalRenderer::SetTerminalSize(int cols, int rows) {
  cols_ = std::max(1, cols);
  rows_ = std::max(1, rows);
}

std::string TerminalRenderer::RenderFrame(const SkBitmap& bitmap) {
  if (bitmap.isNull() || bitmap.width() == 0 || bitmap.height() == 0) {
    return {};
  }

  switch (render_mode_) {
    case TerminalRenderMode::kSixel:
      return RenderSixel(bitmap);
    case TerminalRenderMode::kHalfBlock:
      return RenderHalfBlock(bitmap);
  }
  return RenderHalfBlock(bitmap);
}

std::string TerminalRenderer::RenderHalfBlock(const SkBitmap& bitmap) {

  // Reserve a reasonable buffer size to avoid repeated reallocations.
  // Worst case: ~30 bytes per cell for true-color escapes.
  std::string output;
  output.reserve(static_cast<size_t>(cols_) * rows_ * 35 + 256);

  // Hide cursor and move to home position to avoid flicker.
  output.append(kHideCursor);
  output.append(kCursorHome);

  const int src_w = bitmap.width();
  const int src_h = bitmap.height();

  // Each terminal row represents 2 pixel rows (using half-block character).
  // So the effective pixel height we can represent = rows_ * 2.
  const int pixel_rows = rows_ * 2;

  // Track previous colors to skip redundant escape codes.
  RGB prev_fg = {0, 0, 0};
  RGB prev_bg = {0, 0, 0};
  bool first_cell = true;

  for (int row = 0; row < rows_; ++row) {
    // The two source pixel rows this terminal row maps to.
    int src_y_top = (row * 2) * src_h / pixel_rows;
    int src_y_bot = (row * 2 + 1) * src_h / pixel_rows;

    for (int col = 0; col < cols_; ++col) {
      int src_x = col * src_w / cols_;

      // Top pixel = foreground, bottom pixel = background.
      RGB fg = SamplePixel(bitmap, src_x, src_y_top);
      RGB bg = SamplePixel(bitmap, src_x, src_y_bot);

      // Only emit escape codes when colors change.
      bool fg_changed = first_cell || fg.r != prev_fg.r ||
                        fg.g != prev_fg.g || fg.b != prev_fg.b;
      bool bg_changed = first_cell || bg.r != prev_bg.r ||
                        bg.g != prev_bg.g || bg.b != prev_bg.b;

      if (fg_changed && bg_changed) {
        output.append(AnsiColor(fg, /*foreground=*/true));
        output.append(AnsiColor(bg, /*foreground=*/false));
      } else if (fg_changed) {
        output.append(AnsiColor(fg, /*foreground=*/true));
      } else if (bg_changed) {
        output.append(AnsiColor(bg, /*foreground=*/false));
      }

      output.append(kUpperHalfBlock);

      prev_fg = fg;
      prev_bg = bg;
      first_cell = false;
    }

    // Reset at end of each line and add newline.
    output.append(kReset);
    if (row < rows_ - 1) {
      output.push_back('\n');
    }
    first_cell = true;
  }

  // Show cursor again.
  output.append(kShowCursor);

  return output;
}

TerminalRenderer::RGB TerminalRenderer::SamplePixel(
    const SkBitmap& bitmap,
    int x,
    int y) const {
  x = std::clamp(x, 0, bitmap.width() - 1);
  y = std::clamp(y, 0, bitmap.height() - 1);

  SkColor color = bitmap.getColor(x, y);
  return {static_cast<uint8_t>(SkColorGetR(color)),
          static_cast<uint8_t>(SkColorGetG(color)),
          static_cast<uint8_t>(SkColorGetB(color))};
}

std::string TerminalRenderer::AnsiColor(RGB color,
                                            bool foreground) const {
  switch (color_mode_) {
    case ColorMode::kTrueColor:
      // SGR 38;2;r;g;b for foreground, 48;2;r;g;b for background.
      return base::StringPrintf("\033[%d;2;%d;%d;%dm", foreground ? 38 : 48,
                                color.r, color.g, color.b);

    case ColorMode::k256Color: {
      int idx = Closest256Color(color);
      return base::StringPrintf("\033[%d;5;%dm", foreground ? 38 : 48, idx);
    }

    case ColorMode::kBasic: {
      int idx = ClosestBasicColor(color);
      int code = foreground ? kBasicColors[idx].fg_code
                            : kBasicColors[idx].bg_code;
      return base::StringPrintf("\033[%dm", code);
    }
  }

  return {};
}

// static
int TerminalRenderer::Closest256Color(RGB color) {
  // 256-color palette:
  // 0-15: basic colors (handled separately)
  // 16-231: 6x6x6 color cube
  // 232-255: grayscale ramp

  // Check the 6x6x6 cube.
  auto to_cube = [](uint8_t v) -> int {
    // The 6 cube values are: 0, 95, 135, 175, 215, 255.
    if (v < 48) return 0;
    if (v < 115) return 1;
    if (v < 155) return 2;
    if (v < 195) return 3;
    if (v < 235) return 4;
    return 5;
  };

  int ri = to_cube(color.r);
  int gi = to_cube(color.g);
  int bi = to_cube(color.b);
  int cube_index = 16 + 36 * ri + 6 * gi + bi;

  // Also check grayscale ramp (232-255, 24 shades).
  int gray = (color.r + color.g + color.b) / 3;
  int gray_index = 232 + std::clamp((gray - 8) / 10, 0, 23);

  // Pick whichever is closer.
  static constexpr uint8_t kCubeValues[] = {0, 95, 135, 175, 215, 255};
  int cube_dist = ColorDistanceSq(color.r, color.g, color.b,
                                  kCubeValues[ri], kCubeValues[gi],
                                  kCubeValues[bi]);
  uint8_t gv = static_cast<uint8_t>(8 + (gray_index - 232) * 10);
  int gray_dist = ColorDistanceSq(color.r, color.g, color.b, gv, gv, gv);

  return gray_dist < cube_dist ? gray_index : cube_index;
}

// static
int TerminalRenderer::ClosestBasicColor(RGB color) {
  int best = 0;
  int best_dist = INT_MAX;
  for (int i = 0; i < 16; ++i) {
    int dist = ColorDistanceSq(color.r, color.g, color.b,
                               kBasicColors[i].r, kBasicColors[i].g,
                               kBasicColors[i].b);
    if (dist < best_dist) {
      best_dist = dist;
      best = i;
    }
  }
  return best;
}

std::string TerminalRenderer::RenderSixel(const SkBitmap& bitmap) {
  // Sixel graphics protocol (DEC):
  // - Each "sixel" is a column of 6 pixels, encoded as a single character.
  // - Colors are defined with #N;2;R;G;B (percentages 0-100).
  // - Pixel data: for each color, emit which sixels have that color set.
  // - '$' = carriage return (start of line), '-' = newline (next 6 rows).
  //
  // We quantize to a palette to keep output size reasonable.
  // Format: \033Pq ... \033\\ (DCS introducer ... ST terminator)

  const int src_w = bitmap.width();
  const int src_h = bitmap.height();

  // Quantize to a fixed palette. We use a 6×6×6 color cube (216 colors)
  // which maps well to terminal capabilities and keeps Sixel data compact.
  constexpr int kCubeSide = 6;
  constexpr int kPaletteSize = kCubeSide * kCubeSide * kCubeSide;

  auto quantize = [](uint8_t val) -> int {
    return std::min(val * kCubeSide / 256, kCubeSide - 1);
  };

  auto palette_index = [&](uint8_t r, uint8_t g, uint8_t b) -> int {
    return quantize(r) * kCubeSide * kCubeSide + quantize(g) * kCubeSide +
           quantize(b);
  };

  // Build quantized image.
  std::vector<int> indexed(src_w * src_h);
  for (int y = 0; y < src_h; ++y) {
    for (int x = 0; x < src_w; ++x) {
      SkColor c = bitmap.getColor(x, y);
      indexed[y * src_w + x] = palette_index(
          static_cast<uint8_t>(SkColorGetR(c)),
          static_cast<uint8_t>(SkColorGetG(c)),
          static_cast<uint8_t>(SkColorGetB(c)));
    }
  }

  // Estimate output size: header + palette + data.
  std::string output;
  output.reserve(src_w * src_h / 2 + kPaletteSize * 20 + 256);

  // Move cursor to top-left, hide cursor.
  output.append("\033[?25l");
  output.append("\033[H");

  // DCS q - start sixel data. Set raster attributes for dimensions.
  // \033P0;0;0q - default sixel mode.
  output.append("\033P0;0;0q");

  // Raster attributes: "pan;pad;ph;pv where ph=width, pv=height.
  output.append(base::StringPrintf("\"1;1;%d;%d", src_w, src_h));

  // Define palette: #N;2;R%;G%;B% (percentages 0-100).
  for (int ri = 0; ri < kCubeSide; ++ri) {
    for (int gi = 0; gi < kCubeSide; ++gi) {
      for (int bi = 0; bi < kCubeSide; ++bi) {
        int idx = ri * kCubeSide * kCubeSide + gi * kCubeSide + bi;
        int rp = ri * 100 / (kCubeSide - 1);
        int gp = gi * 100 / (kCubeSide - 1);
        int bp = bi * 100 / (kCubeSide - 1);
        output.append(
            base::StringPrintf("#%d;2;%d;%d;%d", idx, rp, gp, bp));
      }
    }
  }

  // Encode sixel data. Process the image in bands of 6 rows.
  // For each band, for each color present, emit the sixel row.
  for (int band_y = 0; band_y < src_h; band_y += 6) {
    int band_h = std::min(6, src_h - band_y);

    // Track which colors are used in this band for efficiency.
    std::vector<bool> color_used(kPaletteSize, false);
    for (int dy = 0; dy < band_h; ++dy) {
      int y = band_y + dy;
      for (int x = 0; x < src_w; ++x) {
        color_used[indexed[y * src_w + x]] = true;
      }
    }

    for (int ci = 0; ci < kPaletteSize; ++ci) {
      if (!color_used[ci]) {
        continue;
      }

      // Select color.
      output.append(base::StringPrintf("#%d", ci));

      // Build sixel data for this color across the band.
      // Each character encodes 6 vertical bits at one x position.
      // The character is the bit pattern + 63 (0x3F).
      int run_char = -1;
      int run_len = 0;

      for (int x = 0; x < src_w; ++x) {
        int sixel_bits = 0;
        for (int dy = 0; dy < 6; ++dy) {
          int y = band_y + dy;
          if (y < src_h && indexed[y * src_w + x] == ci) {
            sixel_bits |= (1 << dy);
          }
        }
        int ch = sixel_bits + 0x3F;

        // Run-length encode.
        if (ch == run_char) {
          run_len++;
        } else {
          // Flush previous run.
          if (run_len > 0) {
            if (run_len >= 4) {
              output.append(
                  base::StringPrintf("!%d%c", run_len, run_char));
            } else {
              output.append(run_len, static_cast<char>(run_char));
            }
          }
          run_char = ch;
          run_len = 1;
        }
      }

      // Flush last run.
      if (run_len > 0) {
        if (run_len >= 4) {
          output.append(
              base::StringPrintf("!%d%c", run_len, run_char));
        } else {
          output.append(run_len, static_cast<char>(run_char));
        }
      }

      // '$' = CR (back to start of this band for the next color).
      output.push_back('$');
    }

    // '-' = move to next band of 6 rows.
    if (band_y + 6 < src_h) {
      output.push_back('-');
    }
  }

  // ST (String Terminator) - end sixel data.
  output.append("\033\\");

  // Show cursor.
  output.append("\033[?25h");

  return output;
}

}  // namespace ui
