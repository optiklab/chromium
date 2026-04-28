// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/terminal/terminal_surface_factory.h"

#include <memory>

#include "base/logging.h"
#include "components/viz/common/resources/shared_image_format_utils.h"
#include "skia/ext/legacy_display_globals.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkSurfaceProps.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/native_pixmap.h"
#include "ui/gfx/vsync_provider.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/ozone/common/egl_util.h"
#include "ui/ozone/common/gl_ozone_egl.h"
#include "ui/ozone/common/gl_surface_egl_readback.h"
#include "ui/ozone/platform/terminal/terminal_constants.h"
#include "ui/ozone/platform/terminal/terminal_renderer.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace ui {

namespace {

// Query the terminal size in columns and rows.
void GetTerminalSize(int* cols, int* rows) {
#if BUILDFLAG(IS_WIN)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  } else {
    *cols = 120;
    *rows = 40;
  }
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  } else {
    *cols = 120;
    *rows = 40;
  }
#endif
}

// A stub NativePixmap implementation for the terminal platform.
class TerminalPixmap : public gfx::NativePixmap {
 public:
  explicit TerminalPixmap(viz::SharedImageFormat format) : format_(format) {}

  TerminalPixmap(const TerminalPixmap&) = delete;
  TerminalPixmap& operator=(const TerminalPixmap&) = delete;

  bool AreDmaBufFdsValid() const override { return false; }
  int GetDmaBufFd(size_t plane) const override { return -1; }
  uint32_t GetDmaBufPitch(size_t plane) const override { return 0; }
  size_t GetDmaBufOffset(size_t plane) const override { return 0; }
  size_t GetDmaBufPlaneSize(size_t plane) const override { return 0; }
  uint64_t GetFormatModifier() const override { return 0; }
  viz::SharedImageFormat GetSharedImageFormat() const override {
    return format_;
  }
  size_t GetNumberOfPlanes() const override { return format_.NumberOfPlanes(); }
  bool SupportsZeroCopyWebGPUImport() const override { return false; }
  gfx::Size GetBufferSize() const override { return gfx::Size(); }
  uint32_t GetUniqueId() const override { return 0; }
  bool ScheduleOverlayPlane(
      gfx::AcceleratedWidget widget,
      const gfx::OverlayPlaneData& overlay_plane_data,
      std::vector<gfx::GpuFence> acquire_fences,
      std::vector<gfx::GpuFence> release_fences) override {
    return true;
  }
  gfx::NativePixmapHandle ExportHandle() const override {
    return gfx::NativePixmapHandle();
  }

 private:
  ~TerminalPixmap() override = default;

  viz::SharedImageFormat format_;
};

// SurfaceOzoneCanvas that renders to the terminal via ANSI escape sequences.
class TerminalSurface : public SurfaceOzoneCanvas {
 public:
  TerminalSurface() {
    renderer_ = std::make_unique<TerminalRenderer>();
    renderer_->SetRenderMode(TerminalGetRenderMode());
  }
  ~TerminalSurface() override = default;

  // SurfaceOzoneCanvas:
  void ResizeCanvas(const gfx::Size& viewport_size, float scale) override {
    SkSurfaceProps props = skia::LegacyDisplayGlobals::GetSkSurfaceProps();
    surface_ =
        SkSurfaces::Raster(SkImageInfo::MakeN32Premul(viewport_size.width(),
                                                      viewport_size.height()),
                           &props);

    // Update terminal size on each resize.
    int cols, rows;
    GetTerminalSize(&cols, &rows);
    renderer_->SetTerminalSize(cols, rows);
  }

  SkCanvas* GetCanvas() override { return surface_->getCanvas(); }

  void PresentCanvas(const gfx::Rect& damage) override {
    if (!surface_) {
      return;
    }

    SkBitmap bitmap;
    bitmap.allocPixels(surface_->getCanvas()->imageInfo());

    if (surface_->getCanvas()->readPixels(bitmap, 0, 0)) {
      std::string frame = renderer_->RenderFrame(bitmap);
      if (!frame.empty()) {
        // Write the full frame atomically to stdout.
        fwrite(frame.data(), 1, frame.size(), stdout);
        fflush(stdout);
      }
    }
  }

  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override {
    return nullptr;
  }

 private:
  sk_sp<SkSurface> surface_;
  std::unique_ptr<TerminalRenderer> renderer_;
};

// GLOzone implementation that uses EGL with SwiftShader (software GL).
class GLOzoneEGLTerminal : public GLOzoneEGL {
 public:
  GLOzoneEGLTerminal() = default;

  GLOzoneEGLTerminal(const GLOzoneEGLTerminal&) = delete;
  GLOzoneEGLTerminal& operator=(const GLOzoneEGLTerminal&) = delete;

  ~GLOzoneEGLTerminal() override = default;

  // GLOzone:
  scoped_refptr<gl::GLSurface> CreateViewGLSurface(
      gl::GLDisplay* display,
      gfx::AcceleratedWidget window) override {
    return gl::InitializeGLSurface(base::MakeRefCounted<GLSurfaceEglReadback>(
        display->GetAs<gl::GLDisplayEGL>()));
  }

  scoped_refptr<gl::GLSurface> CreateOffscreenGLSurface(
      gl::GLDisplay* display,
      const gfx::Size& size) override {
    return gl::InitializeGLSurface(
        base::MakeRefCounted<gl::PbufferGLSurfaceEGL>(
            display->GetAs<gl::GLDisplayEGL>(), size));
  }

 protected:
  // GLOzoneEGL:
  gl::EGLDisplayPlatform GetNativeDisplay() override {
    return gl::EGLDisplayPlatform(EGL_DEFAULT_DISPLAY);
  }

  bool LoadGLES2Bindings(
      const gl::GLImplementationParts& implementation) override {
    return LoadDefaultEGLGLES2Bindings(implementation);
  }
};

}  // namespace

TerminalSurfaceFactory::TerminalSurfaceFactory()
    : swiftshader_implementation_(std::make_unique<GLOzoneEGLTerminal>()) {}

TerminalSurfaceFactory::~TerminalSurfaceFactory() = default;

std::vector<gl::GLImplementationParts>
TerminalSurfaceFactory::GetAllowedGLImplementations() {
  return std::vector<gl::GLImplementationParts>{
      gl::GLImplementationParts(gl::kGLImplementationEGLANGLE),
  };
}

GLOzone* TerminalSurfaceFactory::GetGLOzone(
    const gl::GLImplementationParts& implementation) {
  switch (implementation.gl) {
    case gl::kGLImplementationEGLGLES2:
    case gl::kGLImplementationEGLANGLE:
      return swiftshader_implementation_.get();
    default:
      return nullptr;
  }
}

std::unique_ptr<SurfaceOzoneCanvas>
TerminalSurfaceFactory::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  return std::make_unique<TerminalSurface>();
}

scoped_refptr<gfx::NativePixmap>
TerminalSurfaceFactory::CreateNativePixmap(
    gfx::AcceleratedWidget widget,
    gpu::VulkanDeviceQueue* device_queue,
    gfx::Size size,
    viz::SharedImageFormat format,
    gfx::BufferUsage usage,
    std::optional<gfx::Size> framebuffer_size) {
  return base::MakeRefCounted<TerminalPixmap>(format);
}

}  // namespace ui
