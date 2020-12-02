// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/native_api/egl/ns_window_bridge.h"
#include "electron/native_api/egl/surface.h"

namespace egl {

Surface::Surface(const Config* config, EGLNativeWindowType win)
    : is_current_in_some_thread_(false),
      config_(config),
      win_(win),
      is_offscreen_(false) {
  window_bridge_ = new NSWindowBridge(reinterpret_cast<NSWindow*>(win));
}

void Surface::Destroy() {
  delete window_bridge_;
  window_bridge_ = nullptr;
}

const gfx::Size Surface::size() {
  return bounds().size();
}

const gfx::Rect Surface::bounds() {
  NSWindow* window = reinterpret_cast<NSWindow*>(win_);
  gfx::Rect new_bounds =
      gfx::Rect([window convertRectToBacking:[window contentLayoutRect]]);
  size_dirty_ = size_dirty_ || (new_bounds.size() != bounds_.size());
  bounds_ = new_bounds;
  return bounds_;
}

float Surface::scale_factor() {
  NSWindow* window = reinterpret_cast<NSWindow*>(win_);
  return [window backingScaleFactor];
}

gpu::SurfaceHandle Surface::window() const {
  return window_bridge_->accelerated_widget();
}

}  // namespace egl
