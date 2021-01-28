// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/native_api/egl/ns_window_bridge.h"

#import <QuartzCore/QuartzCore.h>

#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"

namespace egl {

NSWindowBridge::NSWindowBridge(NSWindow* window)
    : window_([window retain]),
      accelerated_widget_mac_(new ui::AcceleratedWidgetMac()) {
  accelerated_widget_mac_->SetNSView(this);
  NSView* view = [window_ contentView];

  background_layer_.reset([[CALayer alloc] init]);
  display_ca_layer_tree_.reset(
      new ui::DisplayCALayerTree(background_layer_.get()));
  [view setLayer:background_layer_];
  [view setWantsLayer:YES];
}

NSWindowBridge::~NSWindowBridge() {}

gfx::AcceleratedWidget NSWindowBridge::accelerated_widget() {
  return accelerated_widget_mac_->accelerated_widget();
}

void NSWindowBridge::AcceleratedWidgetCALayerParamsUpdated() {
  // Update the contents that the NSView is displaying.
  const gfx::CALayerParams* ca_layer_params =
      accelerated_widget_mac_->GetCALayerParams();
  if (ca_layer_params)
    display_ca_layer_tree_->UpdateCALayerTree(*ca_layer_params);
}

}  // namespace egl
