// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/osr/osr_web_contents_view.h"

#include "content/browser/web_contents/web_contents_impl.h"  // nogncheck
#include "content/public/browser/render_view_host.h"
#include "third_party/blink/public/platform/web_screen_info.h"
#include "ui/display/screen.h"

namespace electron {

OffScreenWebContentsView::OffScreenWebContentsView(
    bool transparent,
    float scale_factor,
    const OnPaintCallback& callback,
    const OnTexturePaintCallback& texture_callback)
    : offscreen_window_(nullptr),
      transparent_(transparent),
      scale_factor_(scale_factor),
      callback_(callback),
      texture_callback_(texture_callback) {
#if defined(OS_MACOSX)
  PlatformCreate();
#endif
}

OffScreenWebContentsView::~OffScreenWebContentsView() {
  if (offscreen_window_)
    offscreen_window_->RemoveObserver(this);

#if defined(OS_MACOSX)
  PlatformDestroy();
#endif
}

void OffScreenWebContentsView::SetWebContents(
    content::WebContents* web_contents) {
  web_contents_ = web_contents;

  RenderViewCreated(web_contents_->GetRenderViewHost());
}

void OffScreenWebContentsView::SetOffscreenWindow(
    api::OffscreenWindow* window) {
  if (offscreen_window_)
    offscreen_window_->RemoveObserver(this);

  offscreen_window_ = window;

  if (offscreen_window_)
    offscreen_window_->AddObserver(this);

  OnWindowResize();
}

void OffScreenWebContentsView::OnWindowResize() {
  // In offscreen mode call RenderWidgetHostView's SetSize explicitly
  if (GetView())
    GetView()->SetSize(GetInitialSize());
}

void OffScreenWebContentsView::OnWindowClosed() {
  if (offscreen_window_) {
    offscreen_window_->RemoveObserver(this);
    offscreen_window_ = nullptr;
  }
}

#if !defined(OS_MACOSX)
gfx::NativeView OffScreenWebContentsView::GetNativeView() const {
  return gfx::NativeView();
}

gfx::NativeView OffScreenWebContentsView::GetContentNativeView() const {
  return gfx::NativeView();
}

gfx::NativeWindow OffScreenWebContentsView::GetTopLevelNativeWindow() const {
  return gfx::NativeWindow();
}
#endif

void OffScreenWebContentsView::GetContainerBounds(gfx::Rect* out) const {
  *out = GetViewBounds();
}

void OffScreenWebContentsView::SizeContents(const gfx::Size& size) {}

void OffScreenWebContentsView::Focus() {}

void OffScreenWebContentsView::SetInitialFocus() {}

void OffScreenWebContentsView::StoreFocus() {}

void OffScreenWebContentsView::RestoreFocus() {}

void OffScreenWebContentsView::FocusThroughTabTraversal(bool reverse) {}

content::DropData* OffScreenWebContentsView::GetDropData() const {
  return nullptr;
}

gfx::Rect OffScreenWebContentsView::GetViewBounds() const {
  if (offscreen_window_) {
    return gfx::Rect(offscreen_window_->GetInternalSize());
  } else {
    return gfx::Rect();
  }
}

void OffScreenWebContentsView::CreateView(gfx::NativeView context) {}

content::RenderWidgetHostViewBase*
OffScreenWebContentsView::CreateViewForWidget(
    content::RenderWidgetHost* render_widget_host) {
  if (render_widget_host->GetView()) {
    return static_cast<content::RenderWidgetHostViewBase*>(
        render_widget_host->GetView());
  }

  return new OffScreenRenderWidgetHostView(
      this, render_widget_host, nullptr, painting_, frame_rate_, scale_factor_);
}

content::RenderWidgetHostViewBase*
OffScreenWebContentsView::CreateViewForChildWidget(
    content::RenderWidgetHost* render_widget_host) {
  content::WebContentsImpl* web_contents_impl =
      static_cast<content::WebContentsImpl*>(web_contents_);

  OffScreenRenderWidgetHostView* view =
      static_cast<OffScreenRenderWidgetHostView*>(
          web_contents_impl->GetOuterWebContents()
              ? web_contents_impl->GetOuterWebContents()
                    ->GetRenderWidgetHostView()
              : web_contents_impl->GetRenderWidgetHostView());

  return new OffScreenRenderWidgetHostView(this, render_widget_host, view,
                                           painting_, view->GetFrameRate(),
                                           scale_factor_);
}

void OffScreenWebContentsView::SetPageTitle(const base::string16& title) {}

void OffScreenWebContentsView::RenderViewCreated(
    content::RenderViewHost* host) {
  if (GetView())
    GetView()->InstallTransparency();
}

void OffScreenWebContentsView::RenderViewReady() {}

void OffScreenWebContentsView::RenderViewHostChanged(
    content::RenderViewHost* old_host,
    content::RenderViewHost* new_host) {
  // Notify new RenderWidgetHostView of the size of the window, since it could
  // have changed since initialization
  OnWindowResize();
}

void OffScreenWebContentsView::SetOverscrollControllerEnabled(bool enabled) {}

#if defined(OS_MACOSX)
bool OffScreenWebContentsView::CloseTabAfterEventTrackingIfNeeded() {
  return false;
}
#endif  // defined(OS_MACOSX)

void OffScreenWebContentsView::StartDragging(
    const content::DropData& drop_data,
    blink::WebDragOperationsMask allowed_ops,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& image_offset,
    const content::DragEventSourceInfo& event_info,
    content::RenderWidgetHostImpl* source_rwh) {
  if (web_contents_)
    web_contents_->SystemDragEnded(source_rwh);
}

void OffScreenWebContentsView::UpdateDragCursor(
    blink::WebDragOperation operation) {}

bool OffScreenWebContentsView::IsTransparent() const {
  return transparent_;
}

const OnPaintCallback& OffScreenWebContentsView::GetPaintCallback() const {
  return callback_;
}

const OnTexturePaintCallback&
OffScreenWebContentsView::GetTexturePaintCallback() const {
  return texture_callback_;
}

gfx::Size OffScreenWebContentsView::GetInitialSize() const {
  if (offscreen_window_) {
    return offscreen_window_->GetInternalSize();
  } else {
    return gfx::Size();
  }
}

void OffScreenWebContentsView::SetPainting(bool painting) {
  auto* view = GetView();
  painting_ = painting;
  if (view != nullptr) {
    view->SetPainting(painting);
  }
}

bool OffScreenWebContentsView::IsPainting() const {
  auto* view = GetView();
  if (view != nullptr) {
    return view->IsPainting();
  } else {
    return painting_;
  }
}

void OffScreenWebContentsView::SetFrameRate(int frame_rate) {
  auto* view = GetView();
  frame_rate_ = frame_rate;
  if (view != nullptr) {
    view->SetFrameRate(frame_rate);
  }
}

int OffScreenWebContentsView::GetFrameRate() const {
  auto* view = GetView();
  if (view != nullptr) {
    return view->GetFrameRate();
  } else {
    return frame_rate_;
  }
}

void OffScreenWebContentsView::SetScaleFactor(float scale_factor) {
  auto* view = GetView();
  if (view != nullptr) {
    view->SetManualScaleFactor(scale_factor);
  } else {
    scale_factor_ = scale_factor;
  }
}

float OffScreenWebContentsView::GetScaleFactor() const {
  auto* view = GetView();
  if (view != nullptr) {
    return view->GetScaleFactor();
  } else {
    return scale_factor_;
  }
}

OffScreenRenderWidgetHostView* OffScreenWebContentsView::GetView() const {
  if (web_contents_) {
    return static_cast<OffScreenRenderWidgetHostView*>(
        web_contents_->GetRenderViewHost()->GetWidget()->GetView());
  }
  return nullptr;
}

}  // namespace electron
