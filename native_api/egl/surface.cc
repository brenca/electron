// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/native_api/egl/surface.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "ui/gl/gl_surface.h"

#if defined(OS_WIN)
#include <windows.h>

namespace {

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  egl::Surface* surface = (egl::Surface*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (msg) {
    case WM_SIZE: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      surface->set_bounds(gfx::Rect(rc));
      break;
    }
  }

  return CallWindowProc(surface->old_wnd_proc_, hwnd, msg, wParam, lParam);
}

}  // namespace
#endif

namespace egl {

Surface::Surface(const Config* config, gfx::Size size)
    : is_current_in_some_thread_(false),
      config_(config),
      bounds_(gfx::Rect(size)),
      is_offscreen_(true) {}

#if !defined(OS_MACOSX)
Surface::Surface(const Config* config, EGLNativeWindowType win)
    : is_current_in_some_thread_(false),
      config_(config),
      win_(win),
      is_offscreen_(false) {
#if defined(OS_WIN)
  SetWindowLongPtr(win_, GWLP_USERDATA, (LONG_PTR)this);
  old_wnd_proc_ =
      (WNDPROC)SetWindowLongPtr(win_, GWLP_WNDPROC, (LONG_PTR)WndProc);
  RECT rc;
  GetClientRect(win_, &rc);
  bounds_ = gfx::Rect(rc);
#else
  NOTREACHED();
#endif
}

gpu::SurfaceHandle Surface::window() const {
#if defined(OS_WIN)
  return win_;
#else
  NOTREACHED();
  return gpu::SurfaceHandle();
#endif
}

void Surface::Destroy() {}
#endif  // !defined(OS_MACOSX)

Surface::~Surface() {
  Destroy();
}

const Config* Surface::config() const {
  return config_;
}

bool Surface::is_offscreen() const {
  return is_offscreen_;
}

#if !defined(OS_MACOSX)
const gfx::Size Surface::size() {
  return bounds_.size();
}

const gfx::Rect Surface::bounds() {
  return bounds_;
}

float Surface::scale_factor() {
  return 1.f;
}
#endif  // !defined(OS_MACOSX)

void Surface::set_bounds(gfx::Rect bounds) {
  bounds_ = bounds;
  size_dirty_ = true;
}

bool Surface::ValidatePbufferAttributeList(const EGLint* attrib_list) {
  if (attrib_list) {
    for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
      switch (attrib_list[i]) {
        case EGL_WIDTH:
        case EGL_HEIGHT:
          break;
        default:
          return false;
      }
    }
  }
  return true;
}

bool Surface::ValidateWindowAttributeList(const EGLint* attrib_list) {
  if (attrib_list) {
    if (attrib_list[0] != EGL_NONE)
      return false;
  }
  return true;
}
}  // namespace egl
