// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_autofill_driver.h"

#include <memory>

#include <utility>

#include "content/public/browser/render_widget_host_view.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/api/electron_api_web_contents_view.h"
#include "shell/browser/native_window.h"

namespace electron {

std::unordered_set<AutofillDriver*> AutofillDriver::drivers_;

AutofillDriver::AutofillDriver(
    content::RenderFrameHost* render_frame_host,
    mojom::ElectronAutofillDriverAssociatedRequest request)
    : render_frame_host_(render_frame_host), binding_(this) {
  drivers_.insert(this);
  autofill_popup_ = std::make_unique<AutofillPopup>();
  binding_.Bind(std::move(request));
}

AutofillDriver::~AutofillDriver() {
  drivers_.erase(this);
}

gfx::RectF AutofillDriver::TransformBoundingBoxToViewportCoordinates(
    const gfx::RectF& bounding_box) {
  content::RenderWidgetHostView* view = render_frame_host_->GetView();
  if (!view)
    return bounding_box;

  gfx::PointF orig_point(bounding_box.x(), bounding_box.y());
  gfx::PointF transformed_point =
      view->TransformPointToRootCoordSpaceF(orig_point);
  return gfx::RectF(transformed_point.x(), transformed_point.y(),
                    bounding_box.width(), bounding_box.height());
}

void AutofillDriver::ShowAutofillPopup(
    const gfx::RectF& bounds,
    const std::vector<base::string16>& values,
    const std::vector<base::string16>& labels) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope scope(isolate);
  auto* web_contents =
      api::WebContents::From(isolate, content::WebContents::FromRenderFrameHost(
                             render_frame_host_)).get();
  if (!web_contents || !web_contents->owner_window())
    return;

  gfx::RectF transformed_box =
      TransformBoundingBoxToViewportCoordinates(bounds);

  auto* embedder = web_contents->embedder();
  bool osr =
      web_contents->IsOffScreen() || (embedder && embedder->IsOffScreen());

  autofill_popup_->CreateView(render_frame_host_, osr,
                              web_contents->owner_window()->content_view(),
                              transformed_box);

  autofill_popup_->SetItems(values, labels);
}

void AutofillDriver::HideAutofillPopup() {
  for (auto* driver : drivers_) {
    if (driver->autofill_popup_)
      driver->autofill_popup_->Hide();
  }
}

}  // namespace electron
