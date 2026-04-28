// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_INPUT_HANDLER_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_INPUT_HANDLER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/threading/thread.h"
#include "ui/events/event.h"

namespace ui {

class PlatformWindowDelegate;

// Reads raw keyboard and mouse input from the terminal (stdin) on a
// background thread and dispatches KeyEvents and MouseEvents to the platform
// window delegate. Supports basic ASCII input, common escape sequences
// (arrow keys), and SGR-mode xterm mouse tracking for click support.
class TerminalInputHandler {
 public:
  using KeyEventCallback = base::RepeatingCallback<void(KeyEvent*)>;
  using MouseEventCallback = base::RepeatingCallback<void(MouseEvent*)>;

  TerminalInputHandler(KeyEventCallback key_callback,
                           MouseEventCallback mouse_callback);
  ~TerminalInputHandler();

  TerminalInputHandler(const TerminalInputHandler&) = delete;
  TerminalInputHandler& operator=(const TerminalInputHandler&) = delete;

  // Start reading from stdin on a background thread.
  void Start();

  // Stop the reader thread.
  void Stop();

 private:
  void ReadLoop();
  void HandleEscapeSequence();
  void HandleSgrMouse();
  int ReadChar();

  KeyEventCallback key_callback_;
  MouseEventCallback mouse_callback_;
  std::unique_ptr<base::Thread> reader_thread_;
  bool running_ = false;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_INPUT_HANDLER_H_
