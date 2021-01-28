// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NATIVE_API_EGL_NS_WINDOW_BRIDGE_H_
#define NATIVE_API_EGL_NS_WINDOW_BRIDGE_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"
#include "ui/accelerated_widget_mac/accelerated_widget_mac.h"
#include "ui/accelerated_widget_mac/display_ca_layer_tree.h"
#include "ui/gfx/geometry/size.h"

namespace egl {

class NSWindowBridge : public ui::AcceleratedWidgetMacNSView {
 public:
  explicit NSWindowBridge(NSWindow* window);
  virtual ~NSWindowBridge();

  gfx::AcceleratedWidget accelerated_widget();

 private:
  void AcceleratedWidgetCALayerParamsUpdated() final;

  base::scoped_nsobject<NSWindow> window_;
  std::unique_ptr<ui::DisplayCALayerTree> display_ca_layer_tree_;
  std::unique_ptr<ui::AcceleratedWidgetMac> accelerated_widget_mac_;
  base::scoped_nsobject<CALayer> background_layer_;
};

}  // namespace egl

#endif  // NATIVE_API_EGL_NS_WINDOW_BRIDGE_H_
