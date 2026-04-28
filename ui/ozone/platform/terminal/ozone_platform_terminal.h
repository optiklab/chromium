// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_OZONE_PLATFORM_TERMINAL_H_
#define UI_OZONE_PLATFORM_TERMINAL_OZONE_PLATFORM_TERMINAL_H_

#include "ui/ozone/public/ozone_platform.h"

namespace ui {

// Constructor hook for use in ozone_platform_list.cc
OzonePlatform* CreateOzonePlatformTerminal();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_OZONE_PLATFORM_TERMINAL_H_
