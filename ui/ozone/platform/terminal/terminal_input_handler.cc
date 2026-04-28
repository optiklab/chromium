// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_input_handler.h"

#include <cstdio>
#include <string>

#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/ozone/platform/terminal/terminal_constants.h"

#if BUILDFLAG(IS_WIN)
#include <conio.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace ui {

namespace {

// xterm escape sequences to enable/disable SGR mouse tracking.
// SGR mode (\033[?1006h) sends coordinates as decimal numbers with no
// upper limit, and distinguishes press ('M') from release ('m').
// We also enable basic button tracking (\033[?1000h) and motion
// tracking (\033[?1002h) for drag support.
constexpr char kEnableMouseTracking[] =
    "\033[?1000h\033[?1002h\033[?1006h";
constexpr char kDisableMouseTracking[] =
    "\033[?1006l\033[?1002l\033[?1000l";

#if !BUILDFLAG(IS_WIN)
struct termios g_original_termios;
bool g_termios_saved = false;

void EnableRawMode() {
  if (!g_termios_saved) {
    tcgetattr(STDIN_FILENO, &g_original_termios);
    g_termios_saved = true;
  }
  struct termios raw = g_original_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void DisableRawMode() {
  if (g_termios_saved) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios);
  }
}
#endif

struct KeyMapping {
  KeyboardCode key_code;
  DomCode dom_code;
};

KeyMapping MapAsciiToKey(int ch) {
  if (ch >= 'a' && ch <= 'z') {
    return {static_cast<KeyboardCode>(VKEY_A + (ch - 'a')),
            static_cast<DomCode>(static_cast<int>(DomCode::US_A) +
                                 (ch - 'a'))};
  }
  if (ch >= 'A' && ch <= 'Z') {
    return {static_cast<KeyboardCode>(VKEY_A + (ch - 'A')),
            static_cast<DomCode>(static_cast<int>(DomCode::US_A) +
                                 (ch - 'A'))};
  }
  if (ch >= '0' && ch <= '9') {
    return {static_cast<KeyboardCode>(VKEY_0 + (ch - '0')),
            static_cast<DomCode>(static_cast<int>(DomCode::DIGIT0) +
                                 (ch - '0'))};
  }
  switch (ch) {
    case '\r':
    case '\n':
      return {VKEY_RETURN, DomCode::ENTER};
    case '\t':
      return {VKEY_TAB, DomCode::TAB};
    case 27:
      return {VKEY_ESCAPE, DomCode::ESCAPE};
    case ' ':
      return {VKEY_SPACE, DomCode::SPACE};
    case 127:
    case 8:
      return {VKEY_BACK, DomCode::BACKSPACE};
    default:
      return {VKEY_UNKNOWN, DomCode::NONE};
  }
}

}  // namespace

TerminalInputHandler::TerminalInputHandler(
    KeyEventCallback key_callback,
    MouseEventCallback mouse_callback)
    : key_callback_(std::move(key_callback)),
      mouse_callback_(std::move(mouse_callback)) {}

TerminalInputHandler::~TerminalInputHandler() {
  Stop();
}

void TerminalInputHandler::Start() {
  if (running_) {
    return;
  }
  running_ = true;

  reader_thread_ = std::make_unique<base::Thread>("TerminalInput");
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  reader_thread_->StartWithOptions(std::move(options));
  reader_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TerminalInputHandler::ReadLoop,
                                base::Unretained(this)));
}

void TerminalInputHandler::Stop() {
  running_ = false;
#if !BUILDFLAG(IS_WIN)
  // Disable mouse tracking and restore terminal.
  fprintf(stderr, "%s", kDisableMouseTracking);
  DisableRawMode();
#endif
  if (reader_thread_) {
    // The UI thread disallows sync primitives after startup, but we need to
    // join the input reader thread to safely restore terminal state.
    base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope allow_join;
    reader_thread_->Stop();
    reader_thread_.reset();
  }
}

int TerminalInputHandler::ReadChar() {
#if BUILDFLAG(IS_WIN)
  if (_kbhit()) {
    return _getch();
  }
  ::Sleep(10);
  return -1;
#else
  int ch = getchar();
  return (ch == EOF) ? -1 : ch;
#endif
}

void TerminalInputHandler::ReadLoop() {
#if !BUILDFLAG(IS_WIN)
  EnableRawMode();
#endif
  // Enable SGR mouse tracking.
  fprintf(stderr, "%s", kEnableMouseTracking);
  fflush(stderr);

  while (running_) {
    int ch = ReadChar();
    if (ch < 0) {
#if BUILDFLAG(IS_WIN)
      continue;
#else
      break;
#endif
    }

    // Escape sequences.
    if (ch == 27) {
      HandleEscapeSequence();
      continue;
    }

    // Regular key.
    KeyMapping mapping = MapAsciiToKey(ch);
    if (mapping.key_code == VKEY_UNKNOWN) {
      continue;
    }

    int flags = EF_NONE;
    if (ch >= 'A' && ch <= 'Z') {
      flags |= EF_SHIFT_DOWN;
    }

    KeyEvent press(EventType::kKeyPressed, mapping.key_code, mapping.dom_code,
                   flags, DomKey::NONE, base::TimeTicks::Now());
    key_callback_.Run(&press);
    KeyEvent release(EventType::kKeyReleased, mapping.key_code,
                     mapping.dom_code, flags, DomKey::NONE,
                     base::TimeTicks::Now());
    key_callback_.Run(&release);
  }
}

void TerminalInputHandler::HandleEscapeSequence() {
  int next = ReadChar();
  if (next < 0) {
    // Bare escape key.
    KeyEvent press(EventType::kKeyPressed, VKEY_ESCAPE, DomCode::ESCAPE,
                   EF_NONE, DomKey::NONE, base::TimeTicks::Now());
    key_callback_.Run(&press);
    KeyEvent release(EventType::kKeyReleased, VKEY_ESCAPE, DomCode::ESCAPE,
                     EF_NONE, DomKey::NONE, base::TimeTicks::Now());
    key_callback_.Run(&release);
    return;
  }

  if (next != '[') {
    // Unknown sequence, send escape.
    KeyEvent press(EventType::kKeyPressed, VKEY_ESCAPE, DomCode::ESCAPE,
                   EF_NONE, DomKey::NONE, base::TimeTicks::Now());
    key_callback_.Run(&press);
    KeyEvent release(EventType::kKeyReleased, VKEY_ESCAPE, DomCode::ESCAPE,
                     EF_NONE, DomKey::NONE, base::TimeTicks::Now());
    key_callback_.Run(&release);
    return;
  }

  // CSI sequence: \033[...
  int third = ReadChar();
  if (third < 0) {
    return;
  }

  // SGR mouse: \033[< followed by params and 'M' or 'm'.
  if (third == '<') {
    HandleSgrMouse();
    return;
  }

  // Arrow keys: \033[A/B/C/D
  // Home/End:   \033[H / \033[F
  KeyMapping mapping = {VKEY_UNKNOWN, DomCode::NONE};
  switch (third) {
    case 'A':
      mapping = {VKEY_UP, DomCode::ARROW_UP};
      break;
    case 'B':
      mapping = {VKEY_DOWN, DomCode::ARROW_DOWN};
      break;
    case 'C':
      mapping = {VKEY_RIGHT, DomCode::ARROW_RIGHT};
      break;
    case 'D':
      mapping = {VKEY_LEFT, DomCode::ARROW_LEFT};
      break;
    case 'H':
      mapping = {VKEY_HOME, DomCode::HOME};
      break;
    case 'F':
      mapping = {VKEY_END, DomCode::END};
      break;
    default:
      break;
  }

  // Extended keys: \033[N~ where N is a number.
  // 5~ = Page Up, 6~ = Page Down, 1~ = Home, 4~ = End
  if (third >= '0' && third <= '9') {
    int fourth = ReadChar();
    if (fourth == '~') {
      switch (third) {
        case '5':
          mapping = {VKEY_PRIOR, DomCode::PAGE_UP};
          break;
        case '6':
          mapping = {VKEY_NEXT, DomCode::PAGE_DOWN};
          break;
        case '1':
          mapping = {VKEY_HOME, DomCode::HOME};
          break;
        case '4':
          mapping = {VKEY_END, DomCode::END};
          break;
        case '3':
          mapping = {VKEY_DELETE, DomCode::DEL};
          break;
        case '2':
          mapping = {VKEY_INSERT, DomCode::INSERT};
          break;
        default:
          break;
      }
    }
  }

  if (mapping.key_code != VKEY_UNKNOWN) {
    KeyEvent press(EventType::kKeyPressed, mapping.key_code, mapping.dom_code,
                   EF_NONE, DomKey::NONE, base::TimeTicks::Now());
    key_callback_.Run(&press);
    KeyEvent release(EventType::kKeyReleased, mapping.key_code,
                     mapping.dom_code, EF_NONE, DomKey::NONE,
                     base::TimeTicks::Now());
    key_callback_.Run(&release);
  }
}

void TerminalInputHandler::HandleSgrMouse() {
  // Parse SGR mouse sequence: \033[<btn;col;rowM  (press)
  //                           \033[<btn;col;rowm  (release)
  // We've already consumed \033[<, now read "btn;col;row" then 'M' or 'm'.
  std::string buf;
  int terminator = -1;
  while (running_) {
    int ch = ReadChar();
    if (ch < 0) {
      return;
    }
    if (ch == 'M' || ch == 'm') {
      terminator = ch;
      break;
    }
    buf.push_back(static_cast<char>(ch));
    // Sanity limit to avoid runaway reads.
    if (buf.size() > 32) {
      return;
    }
  }

  // Parse "btn;col;row".
  int btn = 0, col = 1, row = 1;
  if (sscanf(buf.c_str(), "%d;%d;%d", &btn, &col, &row) != 3) {
    return;
  }

  // Terminal coordinates are 1-based. Convert to 0-based pixel coords.
  // In sixel mode, each cell maps to actual pixel dimensions.
  // In half-block mode, each cell maps to the configured cell pixel size.
  float cell_w;
  float cell_h;

  if (TerminalGetRenderMode() == TerminalRenderMode::kSixel) {
    // In sixel mode, query the terminal's pixel dimensions and divide by
    // cell dimensions to get pixels-per-cell.
    // TIOCGWINSZ gives us ws_xpixel/ws_ypixel on Linux.
    int cols = 120, rows = 40;
    int xpixel = 0, ypixel = 0;
#if !BUILDFLAG(IS_WIN)
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
        ws.ws_row > 0) {
      cols = ws.ws_col;
      rows = ws.ws_row;
      xpixel = ws.ws_xpixel;
      ypixel = ws.ws_ypixel;
    }
#endif
    if (xpixel > 0 && ypixel > 0) {
      cell_w = static_cast<float>(xpixel) / cols;
      cell_h = static_cast<float>(ypixel) / rows;
    } else {
      // Fallback: assume 8×16 pixels per cell.
      cell_w = 8.0f;
      cell_h = 16.0f;
    }
  } else {
    cell_w = static_cast<float>(TerminalCellPixelWidth());
    cell_h = static_cast<float>(TerminalCellPixelHeight());
  }

  float pixel_x = (col - 1) * cell_w + cell_w / 2.0f;
  float pixel_y = (row - 1) * cell_h + cell_h / 2.0f;
  gfx::PointF location(pixel_x, pixel_y);

  // SGR mouse protocol encodes scroll wheel as btn 64 (up) and 65 (down).
  // These don't have press/release semantics — each event is a single tick.
  if (btn == 64 || btn == 65) {
    int y_offset = (btn == 64) ? MouseWheelEvent::kWheelDelta
                               : -MouseWheelEvent::kWheelDelta;
    MouseWheelEvent wheel_event(gfx::Vector2d(0, y_offset), location, location,
                                base::TimeTicks::Now(), EF_NONE, 0);
    mouse_callback_.Run(&wheel_event);
    return;
  }

  // Decode button and modifiers from the btn field.
  // Bits 0-1: button (0=left, 1=middle, 2=right, 3=release/motion)
  // Bit 5: motion event (32)
  // Bits 2-4: modifiers (4=shift, 8=meta, 16=ctrl)
  int button_code = btn & 0x03;
  bool is_motion = (btn & 32) != 0;
  int flags = EF_NONE;
  if (btn & 4) {
    flags |= EF_SHIFT_DOWN;
  }
  if (btn & 8) {
    flags |= EF_ALT_DOWN;
  }
  if (btn & 16) {
    flags |= EF_CONTROL_DOWN;
  }

  int changed_button_flags = 0;
  switch (button_code) {
    case 0:
      flags |= EF_LEFT_MOUSE_BUTTON;
      changed_button_flags = EF_LEFT_MOUSE_BUTTON;
      break;
    case 1:
      flags |= EF_MIDDLE_MOUSE_BUTTON;
      changed_button_flags = EF_MIDDLE_MOUSE_BUTTON;
      break;
    case 2:
      flags |= EF_RIGHT_MOUSE_BUTTON;
      changed_button_flags = EF_RIGHT_MOUSE_BUTTON;
      break;
    default:
      break;
  }

  EventType event_type;
  if (is_motion) {
    event_type = EventType::kMouseDragged;
  } else if (terminator == 'M') {
    event_type = EventType::kMousePressed;
  } else {
    event_type = EventType::kMouseReleased;
  }

  MouseEvent mouse_event(event_type, location, location,
                          base::TimeTicks::Now(), flags,
                          changed_button_flags);
  mouse_callback_.Run(&mouse_event);

  // For clicks, also send a release if we got a press, so elements
  // see a full click cycle (press + release). This is needed because
  // some terminals don't reliably send the release event.
  if (event_type == EventType::kMousePressed && !is_motion) {
    MouseEvent release_event(EventType::kMouseReleased, location, location,
                             base::TimeTicks::Now(), flags,
                             changed_button_flags);
    mouse_callback_.Run(&release_event);
  }
}

}  // namespace ui
