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
      surface->setSize(gfx::Rect(rc).size());
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
      size_(size),
      is_offscreen_(true) {}

Surface::Surface(const Config* config, EGLNativeWindowType win)
    : is_current_in_some_thread_(false),
      config_(config),
      win_(win),
      is_offscreen_(false) {
  SetWindowLongPtr(win_, GWLP_USERDATA, (LONG_PTR)this);
  old_wnd_proc_ =
      (WNDPROC)SetWindowLongPtr(win_, GWLP_WNDPROC, (LONG_PTR)WndProc);
  RECT rc;
  GetClientRect(win_, &rc);
  size_ = gfx::Rect(rc).size();
}

Surface::~Surface() = default;

const Config* Surface::config() const {
  return config_;
}

bool Surface::is_offscreen() const {
  return is_offscreen_;
}

EGLNativeWindowType Surface::window() const {
  return win_;
}

const gfx::Size Surface::size() const {
  return size_;
}

void Surface::setSize(gfx::Size size) {
  size_ = size;
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
