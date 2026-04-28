// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_CLIENT_NATIVE_PIXMAP_FACTORY_TERMINAL_H_
#define UI_OZONE_PLATFORM_TERMINAL_CLIENT_NATIVE_PIXMAP_FACTORY_TERMINAL_H_

namespace gfx {
class ClientNativePixmapFactory;
}

namespace ui {

// Constructor hook for use in constructor_list.cc
gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryTerminal();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_CLIENT_NATIVE_PIXMAP_FACTORY_TERMINAL_H_
