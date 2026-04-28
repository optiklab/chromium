// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_TERMINAL_TERMINAL_SURFACE_FACTORY_H_
#define UI_OZONE_PLATFORM_TERMINAL_TERMINAL_SURFACE_FACTORY_H_

#include <memory>
#include <vector>

#include "ui/ozone/public/gl_ozone.h"
#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

// SurfaceFactory for the terminal Ozone platform. Creates software canvases
// that render pixel content as ANSI-escaped text to stdout.
class TerminalSurfaceFactory : public SurfaceFactoryOzone {
 public:
  TerminalSurfaceFactory();

  TerminalSurfaceFactory(const TerminalSurfaceFactory&) = delete;
  TerminalSurfaceFactory& operator=(const TerminalSurfaceFactory&) =
      delete;

  ~TerminalSurfaceFactory() override;

  // SurfaceFactoryOzone:
  std::vector<gl::GLImplementationParts> GetAllowedGLImplementations() override;
  GLOzone* GetGLOzone(const gl::GLImplementationParts& implementation) override;
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;
  scoped_refptr<gfx::NativePixmap> CreateNativePixmap(
      gfx::AcceleratedWidget widget,
      gpu::VulkanDeviceQueue* device_queue,
      gfx::Size size,
      viz::SharedImageFormat format,
      gfx::BufferUsage usage,
      std::optional<gfx::Size> framebuffer_size) override;

 private:
  std::unique_ptr<GLOzone> swiftshader_implementation_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_TERMINAL_TERMINAL_SURFACE_FACTORY_H_
