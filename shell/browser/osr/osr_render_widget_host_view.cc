// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/osr/osr_render_widget_host_view.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/single_thread_task_runner.h"
#include "base/task/post_task.h"
#include "base/time/time.h"
#include "components/viz/common/features.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/delay_based_time_source.h"
#include "components/viz/common/gl_helper.h"
#include "components/viz/common/quads/render_pass.h"
#include "content/browser/renderer_host/cursor_manager.h"  // nogncheck
#include "content/browser/renderer_host/input/synthetic_gesture_target.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_delegate.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_owner_delegate.h"  // nogncheck
#include "content/common/view_messages.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/context_factory.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/browser/render_process_host.h"
#include "media/base/video_frame.h"
#include "third_party/blink/public/platform/web_input_event.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_type.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/skbitmap_operations.h"
#include "ui/latency/latency_info.h"

namespace electron {

namespace {

const float kDefaultScaleFactor = 1.0;
const float kAutoScaleFactor = 0.0f;

base::TimeDelta TimeDeltaFromHz(double frequency) {
  return base::TimeDelta::FromSeconds(1) / frequency;
}

ui::MouseEvent UiMouseEventFromWebMouseEvent(blink::WebMouseEvent event) {
  ui::EventType type = ui::EventType::ET_UNKNOWN;
  switch (event.GetType()) {
    case blink::WebInputEvent::kMouseDown:
      type = ui::EventType::ET_MOUSE_PRESSED;
      break;
    case blink::WebInputEvent::kMouseUp:
      type = ui::EventType::ET_MOUSE_RELEASED;
      break;
    case blink::WebInputEvent::kMouseMove:
      type = ui::EventType::ET_MOUSE_MOVED;
      break;
    case blink::WebInputEvent::kMouseEnter:
      type = ui::EventType::ET_MOUSE_ENTERED;
      break;
    case blink::WebInputEvent::kMouseLeave:
      type = ui::EventType::ET_MOUSE_EXITED;
      break;
    default:
      type = ui::EventType::ET_UNKNOWN;
      break;
  }

  int button_flags = 0;
  switch (event.button) {
    case blink::WebMouseEvent::Button::kBack:
      button_flags |= ui::EventFlags::EF_BACK_MOUSE_BUTTON;
      break;
    case blink::WebMouseEvent::Button::kForward:
      button_flags |= ui::EventFlags::EF_FORWARD_MOUSE_BUTTON;
      break;
    case blink::WebMouseEvent::Button::kLeft:
      button_flags |= ui::EventFlags::EF_LEFT_MOUSE_BUTTON;
      break;
    case blink::WebMouseEvent::Button::kMiddle:
      button_flags |= ui::EventFlags::EF_MIDDLE_MOUSE_BUTTON;
      break;
    case blink::WebMouseEvent::Button::kRight:
      button_flags |= ui::EventFlags::EF_RIGHT_MOUSE_BUTTON;
      break;
    default:
      button_flags = 0;
      break;
  }

  ui::MouseEvent ui_event(type,
                          gfx::Point(std::floor(event.PositionInWidget().x),
                                     std::floor(event.PositionInWidget().y)),
                          gfx::Point(std::floor(event.PositionInWidget().x),
                                     std::floor(event.PositionInWidget().y)),
                          ui::EventTimeForNow(), button_flags, button_flags);
  if (event.click_count > 0)
    ui_event.SetClickCount(event.click_count);

  return ui_event;
}

ui::MouseWheelEvent UiMouseWheelEventFromWebMouseEvent(
    blink::WebMouseWheelEvent event) {
  return ui::MouseWheelEvent(UiMouseEventFromWebMouseEvent(event),
                             std::floor(event.delta_x),
                             std::floor(event.delta_y));
}

}  // namespace

class ElectronDelegatedFrameHostClient
    : public content::DelegatedFrameHostClient {
 public:
  explicit ElectronDelegatedFrameHostClient(OffScreenRenderWidgetHostView* view)
      : view_(view) {}

  ui::Layer* DelegatedFrameHostGetLayer() const override {
    return view_->GetRootLayer();
  }

  bool DelegatedFrameHostIsVisible() const override {
    return view_->IsShowing();
  }

  SkColor DelegatedFrameHostGetGutterColor() const override {
    if (view_->render_widget_host()->delegate() &&
        view_->render_widget_host()->delegate()->IsFullscreenForCurrentTab()) {
      return SK_ColorWHITE;
    }
    return *view_->GetBackgroundColor();
  }

  void OnFrameTokenChanged(uint32_t frame_token) override {
    view_->render_widget_host()->DidProcessFrame(frame_token);
  }

  float GetDeviceScaleFactor() const override {
    return view_->GetDeviceScaleFactor();
  }

  std::vector<viz::SurfaceId> CollectSurfaceIdsForEviction() override {
    return view_->render_widget_host()->CollectSurfaceIdsForEviction();
  }

  bool ShouldShowStaleContentOnEviction() override { return false; }

  void OnBeginFrame(base::TimeTicks frame_time) override {}
  void InvalidateLocalSurfaceIdOnEviction() override {}

 private:
  OffScreenRenderWidgetHostView* const view_;

  DISALLOW_COPY_AND_ASSIGN(ElectronDelegatedFrameHostClient);
};

class StandaloneInitializer
    : public OffScreenRenderWidgetHostView::Initializer {
 public:
  StandaloneInitializer(bool transparent,
                        OnPaintCallback paint_callback,
                        OnTexturePaintCallback texture_paint_callback,
                        gfx::Size initial_size)
      : transparent_(transparent),
        paint_callback_(paint_callback),
        texture_paint_callback_(texture_paint_callback),
        initial_size_(initial_size) {}

  bool IsTransparent() const override { return transparent_; }

  const OnPaintCallback& GetPaintCallback() const override {
    return paint_callback_;
  }

  const OnTexturePaintCallback& GetTexturePaintCallback() const override {
    return texture_paint_callback_;
  }

  gfx::Size GetInitialSize() const override { return initial_size_; }

 private:
  bool transparent_;
  OnPaintCallback paint_callback_;
  OnTexturePaintCallback texture_paint_callback_;
  gfx::Size initial_size_;
};

OffScreenRenderWidgetHostView::OffScreenRenderWidgetHostView(
    Initializer* initializer,
    content::RenderWidgetHost* host,
    OffScreenRenderWidgetHostView* parent,
    bool painting,
    int frame_rate,
    float scale_factor)
    : content::RenderWidgetHostViewBase(host),
      render_widget_host_(content::RenderWidgetHostImpl::From(host)),
      parent_host_view_(parent),
      frame_rate_(frame_rate),
      painting_(painting),
      is_showing_(false),
      cursor_manager_(new content::CursorManager(this)),
      mouse_wheel_phase_handler_(this),
      backing_(new SkBitmap),
      weak_ptr_factory_(this) {
  DCHECK(render_widget_host_);
  DCHECK(!render_widget_host_->GetView());

  render_widget_host_->SetView(this);

  transparent_ = initializer->IsTransparent();
  callback_ = initializer->GetPaintCallback();
  texture_callback_ = initializer->GetTexturePaintCallback();
  size_ = initializer->GetInitialSize();

  manual_device_scale_factor_ = scale_factor;
  current_device_scale_factor_ = kDefaultScaleFactor;

  delegated_frame_host_allocator_.GenerateId();
  delegated_frame_host_allocation_ =
      delegated_frame_host_allocator_.GetCurrentLocalSurfaceIdAllocation();
  compositor_allocator_.GenerateId();
  compositor_allocation_ =
      compositor_allocator_.GetCurrentLocalSurfaceIdAllocation();

  delegated_frame_host_client_ =
      std::make_unique<ElectronDelegatedFrameHostClient>(this);
  delegated_frame_host_ = std::make_unique<content::DelegatedFrameHost>(
      AllocateFrameSinkId(), delegated_frame_host_client_.get(),
      true /* should_register_frame_sink_id */);

  root_layer_ = std::make_unique<ui::Layer>(ui::LAYER_SOLID_COLOR);

  bool opaque = SkColorGetA(background_color_) == SK_AlphaOPAQUE;
  GetRootLayer()->SetFillsBoundsOpaquely(opaque);
  GetRootLayer()->SetColor(background_color_);

  ui::ContextFactoryPrivate* context_factory_private =
      content::GetContextFactoryPrivate();
  compositor_ = std::make_unique<ui::Compositor>(
      context_factory_private->AllocateFrameSinkId(),
      content::GetContextFactory(), context_factory_private,
      base::ThreadTaskRunnerHandle::Get(), false /* enable_pixel_canvas */,
      false /* use_external_begin_frame_control */);
  compositor_->SetAcceleratedWidget(gfx::kNullAcceleratedWidget);
  compositor_->SetDelegate(this);
  compositor_->SetRootLayer(root_layer_.get());

  // time_source_ = std::make_unique<viz::DelayBasedTimeSource>(
  //     compositor_->task_runner().get());
  // time_source_->SetClient(this);
  //
  // background_time_source_ = std::make_unique<viz::DelayBasedTimeSource>(
  //     compositor_->task_runner().get());
  // background_time_source_->SetClient(this);
  // background_time_source_->SetTimebaseAndInterval(
  //   base::TimeTicks::Now(),
  //   TimeDeltaFromHz(0.2));

  ResizeRootLayer(false);
  InstallTransparency();

  render_widget_host_->GetProcess()->SetPriorityOverride(true);

  // if (content::GpuDataManager::GetInstance()->HardwareAccelerationEnabled())
  // {
  //   video_consumer_ = std::make_unique<OffScreenVideoConsumer>(
  //       this,
  //       base::BindRepeating(&OffScreenRenderWidgetHostView::OnPaint,
  //                           weak_ptr_factory_.GetWeakPtr()),
  //       base::BindRepeating(&OffScreenRenderWidgetHostView::OnTexturePaint,
  //                           weak_ptr_factory_.GetWeakPtr()));
  //   video_consumer_->SetActive(IsPainting());
  //   video_consumer_->SetFrameRate(GetFrameRate());
  // }

  // time_source_->SetActive(painting_);
  // background_time_source_->SetActive(!painting_);

  // Let the page-level input event router know about our surface ID
  // namespace for surface-based hit testing.
  if (render_widget_host_->delegate() &&
      render_widget_host_->delegate()->GetInputEventRouter()) {
    render_widget_host_->delegate()->GetInputEventRouter()->AddFrameSinkIdOwner(
        GetFrameSinkId(), this);
  }
}

OffScreenRenderWidgetHostView::~OffScreenRenderWidgetHostView() {
  // time_source_->SetActive(false);
  // background_time_source_->SetActive(false);

  // Marking the DelegatedFrameHost as removed from the window hierarchy is
  // necessary to remove all connections to its old ui::Compositor.
  if (is_showing_)
    delegated_frame_host_->WasHidden(
        content::DelegatedFrameHost::HiddenCause::kOther);
  delegated_frame_host_->DetachFromCompositor();

  delegated_frame_host_.reset();
  compositor_.reset();
  root_layer_.reset();
}

content::BrowserAccessibilityManager*
OffScreenRenderWidgetHostView::CreateBrowserAccessibilityManager(
    content::BrowserAccessibilityDelegate*,
    bool) {
  return nullptr;
}

void OffScreenRenderWidgetHostView::OnTimerTick() {
  // LOG(INFO) << compositor_ << can_send_frame_ << '\n';
  if (compositor_ && can_send_frame_) {
    can_send_frame_ = false;

    auto args = viz::BeginFrameArgs::Create(
        BEGINFRAME_FROM_HERE, begin_frame_source_.source_id(),
        begin_frame_sequence_number_, base::TimeTicks::Now(), base::TimeTicks(),
        TimeDeltaFromHz(frame_rate_), viz::BeginFrameArgs::NORMAL);

    begin_frame_sequence_number_++;

    compositor_->context_factory_private()->IssueExternalBeginFrame(
        compositor_.get(), args, true,
        base::BindOnce(&OffScreenRenderWidgetHostView::OnFrameAck,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void OffScreenRenderWidgetHostView::OnFrameAck(const viz::BeginFrameAck& ack) {
  can_send_frame_ = true;
}

void OffScreenRenderWidgetHostView::InitAsChild(gfx::NativeView) {
  DCHECK(parent_host_view_);

  if (parent_host_view_->child_host_view_) {
    parent_host_view_->child_host_view_->CancelWidget();
  }

  parent_host_view_->set_child_host_view(this);
  parent_host_view_->Hide();

  ResizeRootLayer(false);
  SetPainting(parent_host_view_->IsPainting());
}

void OffScreenRenderWidgetHostView::SetSize(const gfx::Size& size) {
  size_ = size;
  SynchronizeVisualProperties();
}

void OffScreenRenderWidgetHostView::SetBounds(const gfx::Rect& new_bounds) {
  SetSize(new_bounds.size());
}

gfx::NativeView OffScreenRenderWidgetHostView::GetNativeView() {
  return gfx::NativeView();
}

gfx::NativeViewAccessible
OffScreenRenderWidgetHostView::GetNativeViewAccessible() {
  return gfx::NativeViewAccessible();
}

ui::TextInputClient* OffScreenRenderWidgetHostView::GetTextInputClient() {
  return nullptr;
}

void OffScreenRenderWidgetHostView::Focus() {}

bool OffScreenRenderWidgetHostView::HasFocus() {
  return false;
}

bool OffScreenRenderWidgetHostView::IsSurfaceAvailableForCopy() {
  return GetDelegatedFrameHost()->CanCopyFromCompositingSurface();
}

void OffScreenRenderWidgetHostView::Show() {
  if (is_showing_)
    return;

  is_showing_ = true;

  delegated_frame_host_->AttachToCompositor(compositor_.get());
  delegated_frame_host_->WasShown(
      GetLocalSurfaceIdAllocation().local_surface_id(),
      GetRootLayer()->bounds().size(), base::nullopt);

  if (render_widget_host_)
    render_widget_host_->WasShown(base::nullopt);
}

void OffScreenRenderWidgetHostView::Hide() {
  if (!is_showing_)
    return;

  if (render_widget_host_)
    render_widget_host_->WasHidden();

  // TODO(deermichel): correct or kOther?
  GetDelegatedFrameHost()->WasHidden(
      content::DelegatedFrameHost::HiddenCause::kOccluded);
  GetDelegatedFrameHost()->DetachFromCompositor();

  is_showing_ = false;
}

bool OffScreenRenderWidgetHostView::IsShowing() {
  return is_showing_;
}

void OffScreenRenderWidgetHostView::EnsureSurfaceSynchronizedForWebTest() {
  ++latest_capture_sequence_number_;
  SynchronizeVisualProperties();
}

gfx::Rect OffScreenRenderWidgetHostView::GetViewBounds() {
  if (IsPopupWidget())
    return popup_position_;

  return gfx::Rect(size_);
}

void OffScreenRenderWidgetHostView::SetBackgroundColor(SkColor color) {
  // The renderer will feed its color back to us with the first CompositorFrame.
  // We short-cut here to show a sensible color before that happens.
  UpdateBackgroundColorFromRenderer(color);

  if (render_widget_host_ && render_widget_host_->owner_delegate()) {
    render_widget_host_->owner_delegate()->SetBackgroundOpaque(
        SkColorGetA(color) == SK_AlphaOPAQUE);
  }
}

base::Optional<SkColor> OffScreenRenderWidgetHostView::GetBackgroundColor() {
  return background_color_;
}

void OffScreenRenderWidgetHostView::UpdateBackgroundColor() {
  NOTREACHED();
}

gfx::Size OffScreenRenderWidgetHostView::GetVisibleViewportSize() {
  return size_;
}

void OffScreenRenderWidgetHostView::SetInsets(const gfx::Insets& insets) {}

bool OffScreenRenderWidgetHostView::LockMouse(
    bool request_unadjusted_movement) {
  return false;
}

void OffScreenRenderWidgetHostView::UnlockMouse() {}

void OffScreenRenderWidgetHostView::TakeFallbackContentFrom(
    content::RenderWidgetHostView* view) {
  DCHECK(!static_cast<content::RenderWidgetHostViewBase*>(view)
              ->IsRenderWidgetHostViewChildFrame());
  OffScreenRenderWidgetHostView* view_osr =
      static_cast<OffScreenRenderWidgetHostView*>(view);
  SetBackgroundColor(view_osr->background_color_);
  if (GetDelegatedFrameHost() && view_osr->GetDelegatedFrameHost()) {
    GetDelegatedFrameHost()->TakeFallbackContentFrom(
        view_osr->GetDelegatedFrameHost());
  }
  host()->GetContentRenderingTimeoutFrom(view_osr->host());
}

void OffScreenRenderWidgetHostView::DidCreateNewRendererCompositorFrameSink(
    viz::mojom::CompositorFrameSinkClient* renderer_compositor_frame_sink) {
  renderer_compositor_frame_sink_ = renderer_compositor_frame_sink;

  if (GetDelegatedFrameHost()) {
    GetDelegatedFrameHost()->DidCreateNewRendererCompositorFrameSink(
        renderer_compositor_frame_sink_);
  }
}

void OffScreenRenderWidgetHostView::SubmitCompositorFrame(
    const viz::LocalSurfaceId& local_surface_id,
    viz::CompositorFrame frame,
    base::Optional<viz::HitTestRegionList> hit_test_region_list) {
  NOTREACHED();
}
void OffScreenRenderWidgetHostView::ResetFallbackToFirstNavigationSurface() {
  GetDelegatedFrameHost()->ResetFallbackToFirstNavigationSurface();
}

void OffScreenRenderWidgetHostView::InitAsPopup(
    content::RenderWidgetHostView* parent_host_view,
    const gfx::Rect& pos) {
  DCHECK_EQ(parent_host_view_, parent_host_view);
  DCHECK_EQ(widget_type_, content::WidgetType::kPopup);

  if (parent_host_view_->popup_host_view_) {
    parent_host_view_->popup_host_view_->CancelWidget();
  }

  parent_host_view_->set_popup_host_view(this);
  parent_callback_ =
      base::BindRepeating(&OffScreenRenderWidgetHostView::OnPopupPaint,
                          parent_host_view_->weak_ptr_factory_.GetWeakPtr());

  parent_texture_callback_ =
      base::BindRepeating(&OffScreenRenderWidgetHostView::OnPopupTexturePaint,
                          parent_host_view_->weak_ptr_factory_.GetWeakPtr());

  popup_position_ = pos;

  ResizeRootLayer(false);
  if (video_consumer_) {
    video_consumer_->SizeChanged();
  }
  Show();
}

void OffScreenRenderWidgetHostView::InitAsFullscreen(
    content::RenderWidgetHostView*) {}

void OffScreenRenderWidgetHostView::UpdateCursor(const content::WebCursor&) {}

content::CursorManager* OffScreenRenderWidgetHostView::GetCursorManager() {
  return cursor_manager_.get();
}

void OffScreenRenderWidgetHostView::SetIsLoading(bool loading) {}

void OffScreenRenderWidgetHostView::TextInputStateChanged(
    const content::TextInputState& params) {}

void OffScreenRenderWidgetHostView::ImeCancelComposition() {}

void OffScreenRenderWidgetHostView::RenderProcessGone() {
  Destroy();
}

void OffScreenRenderWidgetHostView::Destroy() {
  if (!is_destroyed_) {
    is_destroyed_ = true;

    if (parent_host_view_ != nullptr) {
      CancelWidget();
    } else {
      if (popup_host_view_)
        popup_host_view_->CancelWidget();
      if (child_host_view_)
        child_host_view_->CancelWidget();
      for (auto* proxy_view : proxy_views_)
        proxy_view->RemoveObserver();
      Hide();
    }
  }

  delete this;
}

void OffScreenRenderWidgetHostView::SetTooltipText(const base::string16&) {}

uint32_t OffScreenRenderWidgetHostView::GetCaptureSequenceNumber() const {
  return latest_capture_sequence_number_;
}

void OffScreenRenderWidgetHostView::CopyFromSurface(
    const gfx::Rect& src_rect,
    const gfx::Size& output_size,
    base::OnceCallback<void(const SkBitmap&)> callback) {
  GetDelegatedFrameHost()->CopyFromCompositingSurface(src_rect, output_size,
                                                      std::move(callback));
}

void OffScreenRenderWidgetHostView::GetScreenInfo(
    content::ScreenInfo* screen_info) {
  screen_info->depth = 24;
  screen_info->depth_per_component = 8;
  screen_info->orientation_angle = 0;
  screen_info->device_scale_factor = GetScaleFactor();
  screen_info->orientation_type =
      content::SCREEN_ORIENTATION_VALUES_LANDSCAPE_PRIMARY;
  screen_info->rect = gfx::Rect(size_);
  screen_info->available_rect = gfx::Rect(size_);
}

void OffScreenRenderWidgetHostView::TransformPointToRootSurface(
    gfx::PointF* point) {}

gfx::Rect OffScreenRenderWidgetHostView::GetBoundsInRootWindow() {
  return gfx::Rect(size_);
}

viz::SurfaceId OffScreenRenderWidgetHostView::GetCurrentSurfaceId() const {
  return GetDelegatedFrameHost()
             ? GetDelegatedFrameHost()->GetCurrentSurfaceId()
             : viz::SurfaceId();
}

std::unique_ptr<content::SyntheticGestureTarget>
OffScreenRenderWidgetHostView::CreateSyntheticGestureTarget() {
  NOTIMPLEMENTED();
  return nullptr;
}

void OffScreenRenderWidgetHostView::ImeCompositionRangeChanged(
    const gfx::Range&,
    const std::vector<gfx::Rect>&) {}

gfx::Size OffScreenRenderWidgetHostView::GetCompositorViewportPixelSize() {
  return gfx::ScaleToCeiledSize(GetRequestedRendererSize(), GetScaleFactor());
}

content::RenderWidgetHostViewBase*
OffScreenRenderWidgetHostView::CreateViewForWidget(
    content::RenderWidgetHost* render_widget_host,
    content::RenderWidgetHost* embedder_render_widget_host,
    content::WebContentsView* web_contents_view) {
  if (render_widget_host->GetView()) {
    return static_cast<content::RenderWidgetHostViewBase*>(
        render_widget_host->GetView());
  }

  OffScreenRenderWidgetHostView* embedder_host_view = nullptr;
  if (embedder_render_widget_host) {
    embedder_host_view = static_cast<OffScreenRenderWidgetHostView*>(
        embedder_render_widget_host->GetView());
  }

  StandaloneInitializer initializer(transparent_, callback_, texture_callback_,
                                    size());
  return new OffScreenRenderWidgetHostView(
      &initializer, render_widget_host, embedder_host_view, true,
      embedder_host_view->GetFrameRate(), embedder_host_view->GetScaleFactor());
}

const viz::FrameSinkId& OffScreenRenderWidgetHostView::GetFrameSinkId() const {
  return GetDelegatedFrameHost()->frame_sink_id();
}

viz::FrameSinkId OffScreenRenderWidgetHostView::GetRootFrameSinkId() {
  return GetCompositor()->frame_sink_id();
}

void OffScreenRenderWidgetHostView::DidNavigate() {
  ResizeRootLayer(true);
  if (delegated_frame_host_)
    delegated_frame_host_->DidNavigate();
}

bool OffScreenRenderWidgetHostView::TransformPointToCoordSpaceForView(
    const gfx::PointF& point,
    RenderWidgetHostViewBase* target_view,
    gfx::PointF* transformed_point) {
  if (target_view == this) {
    *transformed_point = point;
    return true;
  }

  return false;
}

void OffScreenRenderWidgetHostView::CancelWidget() {
  if (render_widget_host_)
    render_widget_host_->LostCapture();
  Hide();

  if (parent_host_view_) {
    if (parent_host_view_->popup_host_view_ == this) {
      parent_texture_callback_.Run(gpu::Mailbox(), gpu::SyncToken(),
                                   gfx::Rect(), nullptr, nullptr);

      parent_host_view_->set_popup_host_view(nullptr);
    } else if (parent_host_view_->child_host_view_ == this) {
      parent_host_view_->set_child_host_view(nullptr);
      parent_host_view_->Show();
    }
    parent_host_view_ = nullptr;
  }

  if (render_widget_host_ && !is_destroyed_) {
    is_destroyed_ = true;
    // Results in a call to Destroy().
    render_widget_host_->ShutdownAndDestroyWidget(true);
  }
}

void OffScreenRenderWidgetHostView::AddViewProxy(OffscreenViewProxy* proxy) {
  proxy->SetObserver(this);
  proxy_views_.insert(proxy);
}

void OffScreenRenderWidgetHostView::RemoveViewProxy(OffscreenViewProxy* proxy) {
  proxy->RemoveObserver();
  proxy_views_.erase(proxy);
}

void OffScreenRenderWidgetHostView::ProxyViewDestroyed(
    OffscreenViewProxy* proxy) {
  proxy_views_.erase(proxy);
  CompositeFrame(gfx::Rect(GetRequestedRendererSize()));
}

std::unique_ptr<viz::HostDisplayClient>
OffScreenRenderWidgetHostView::CreateHostDisplayClient(
    ui::Compositor* compositor) {
  host_display_client_ = new OffScreenHostDisplayClient(
      gfx::kNullAcceleratedWidget,
      base::BindRepeating(&OffScreenRenderWidgetHostView::OnPaint,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&OffScreenRenderWidgetHostView::OnTexturePaint,
                          weak_ptr_factory_.GetWeakPtr()));
  host_display_client_->SetActive(IsPainting());
  return base::WrapUnique(host_display_client_);
}

bool OffScreenRenderWidgetHostView::InstallTransparency() {
  if (transparent_) {
    SetBackgroundColor(SkColor());
    compositor_->SetBackgroundColor(SK_ColorTRANSPARENT);
    return true;
  }
  return false;
}

void OffScreenRenderWidgetHostView::SetNeedsBeginFrames(
    bool needs_begin_frames) {
  SetupFrameRate();

  if (host_display_client_) {
    host_display_client_->SetActive(IsPainting());
  }
}

void OffScreenRenderWidgetHostView::SetWantsAnimateOnlyBeginFrames() {
  if (delegated_frame_host_) {
    delegated_frame_host_->SetWantsAnimateOnlyBeginFrames();
  }
}
#if defined(OS_MACOSX)
void OffScreenRenderWidgetHostView::SetActive(bool active) {}

void OffScreenRenderWidgetHostView::ShowDefinitionForSelection() {}

void OffScreenRenderWidgetHostView::SpeakSelection() {}

bool OffScreenRenderWidgetHostView::UpdateNSViewAndDisplay() {
  return false;
}
#endif

void OffScreenRenderWidgetHostView::OnPaint(const gfx::Rect& damage_rect,
                                            const SkBitmap& bitmap) {
  backing_ = std::make_unique<SkBitmap>();
  backing_->allocN32Pixels(bitmap.width(), bitmap.height(), !transparent_);
  bitmap.readPixels(backing_->pixmap());

  if (IsPopupWidget()) {
    if (parent_callback_) {
      parent_callback_.Run(this->popup_position_);
    } else {
      // Popup is not yet initialized, reset backing
      backing_ = std::make_unique<SkBitmap>();
    }
  } else {
    CompositeFrame(damage_rect);
  }
}

void OffScreenRenderWidgetHostView::OnPopupTexturePaint(
    const gpu::Mailbox& mailbox,
    const gpu::SyncToken& sync_token,
    const gfx::Rect& content_rect,
    void (*callback)(void*, void*),
    void* context) {
  texture_callback_.Run(std::move(mailbox), std::move(sync_token),
                        std::move(content_rect), true, callback, context);
}

void OffScreenRenderWidgetHostView::OnTexturePaint(
    const gpu::Mailbox& mailbox,
    const gpu::SyncToken& sync_token,
    const gfx::Rect& content_rect,
    void (*callback)(void*, void*),
    void* context) {
  if (!IsPopupWidget()) {
    texture_callback_.Run(std::move(mailbox), std::move(sync_token),
                          std::move(content_rect), false, callback, context);
  } else if (parent_texture_callback_) {
    gfx::Rect rect_in_pixels =
        gfx::ConvertRectToPixel(GetScaleFactor(), popup_position_);

    parent_texture_callback_.Run(
        std::move(mailbox), std::move(sync_token),
        gfx::Rect(rect_in_pixels.origin(), content_rect.size()), callback,
        context);
  } else {
    // TODO: fix this
  }
}

gfx::Size OffScreenRenderWidgetHostView::SizeInPixels() {
  if (IsPopupWidget()) {
    return gfx::ConvertSizeToPixel(GetScaleFactor(), popup_position_.size());
  } else {
    return gfx::ConvertSizeToPixel(GetScaleFactor(), GetViewBounds().size());
  }
}

void OffScreenRenderWidgetHostView::CompositeFrame(
    const gfx::Rect& damage_rect) {
  HoldResize();

  gfx::Size size_in_pixels = SizeInPixels();
  gfx::Rect damage_rect_union = damage_rect;

  SkBitmap frame;

  // Optimize for the case when there is no popup
  if (proxy_views_.size() == 0 && !popup_host_view_) {
    frame = GetBacking();
  } else {
    frame.allocN32Pixels(size_in_pixels.width(), size_in_pixels.height(),
                         false);
    if (!GetBacking().drawsNothing()) {
      SkCanvas canvas(frame);
      canvas.writePixels(GetBacking(), 0, 0);

      if (popup_host_view_ && !popup_host_view_->GetBacking().drawsNothing()) {
        gfx::Rect rect_in_pixels = gfx::ConvertRectToPixel(
            GetScaleFactor(), popup_host_view_->popup_position_);
        damage_rect_union.Union(rect_in_pixels);
        canvas.writePixels(popup_host_view_->GetBacking(),
                           rect_in_pixels.origin().x(),
                           rect_in_pixels.origin().y());
      }

      for (auto* proxy_view : proxy_views_) {
        gfx::Rect rect_in_pixels =
            gfx::ConvertRectToPixel(GetScaleFactor(), proxy_view->GetBounds());
        damage_rect_union.Union(rect_in_pixels);
        canvas.writePixels(*proxy_view->GetBitmap(),
                           rect_in_pixels.origin().x(),
                           rect_in_pixels.origin().y());
      }
    }
  }

  gfx::Rect damage =
      gfx::IntersectRects(gfx::Rect(size_in_pixels), damage_rect_union);

  paint_callback_running_ = true;
  callback_.Run(damage, frame);
  paint_callback_running_ = false;

  ReleaseResize();
}

void OffScreenRenderWidgetHostView::OnPopupPaint(const gfx::Rect& damage_rect) {
  CompositeFrame(gfx::ConvertRectToPixel(GetScaleFactor(), damage_rect));
}

void OffScreenRenderWidgetHostView::OnProxyViewPaint(
    const gfx::Rect& damage_rect) {
  CompositeFrame(gfx::ConvertRectToPixel(GetScaleFactor(), damage_rect));
}

void OffScreenRenderWidgetHostView::HoldResize() {
  if (!hold_resize_)
    hold_resize_ = true;
}

void OffScreenRenderWidgetHostView::ReleaseResize() {
  if (!hold_resize_)
    return;

  hold_resize_ = false;
  if (pending_resize_) {
    pending_resize_ = false;
    base::PostTask(
        FROM_HERE, {content::BrowserThread::UI},
        base::BindOnce(
            &OffScreenRenderWidgetHostView::SynchronizeVisualProperties,
            weak_ptr_factory_.GetWeakPtr()));
  }
}

void OffScreenRenderWidgetHostView::SynchronizeVisualProperties() {
  if (hold_resize_) {
    if (!pending_resize_)
      pending_resize_ = true;
    return;
  }

  ResizeRootLayer(true);
}

void OffScreenRenderWidgetHostView::SendMouseEvent(
    const blink::WebMouseEvent& event) {
  for (auto* proxy_view : proxy_views_) {
    gfx::Rect bounds = proxy_view->GetBounds();
    if (bounds.Contains(event.PositionInWidget().x,
                        event.PositionInWidget().y)) {
      blink::WebMouseEvent proxy_event(event);
      proxy_event.SetPositionInWidget(
          proxy_event.PositionInWidget().x - bounds.x(),
          proxy_event.PositionInWidget().y - bounds.y());

      ui::MouseEvent ui_event = UiMouseEventFromWebMouseEvent(proxy_event);
      proxy_view->OnEvent(&ui_event);
      return;
    }
  }

  if (!IsPopupWidget()) {
    if (popup_host_view_ &&
        popup_host_view_->popup_position_.Contains(
            event.PositionInWidget().x, event.PositionInWidget().y)) {
      blink::WebMouseEvent popup_event(event);
      popup_event.SetPositionInWidget(
          popup_event.PositionInWidget().x -
              popup_host_view_->popup_position_.x(),
          popup_event.PositionInWidget().y -
              popup_host_view_->popup_position_.y());

      popup_host_view_->ProcessMouseEvent(popup_event, ui::LatencyInfo());
      return;
    }
  }

  if (render_widget_host_ && render_widget_host_->GetView()) {
    if (ShouldRouteEvents()) {
      render_widget_host_->delegate()->GetInputEventRouter()->RouteMouseEvent(
          this, const_cast<blink::WebMouseEvent*>(&event),
          ui::LatencyInfo(ui::SourceEventType::OTHER));
    } else {
      render_widget_host_->GetView()->ProcessMouseEvent(
          event, ui::LatencyInfo(ui::SourceEventType::OTHER));
    }
  }
}

void OffScreenRenderWidgetHostView::SendMouseWheelEvent(
    const blink::WebMouseWheelEvent& event) {
  for (auto* proxy_view : proxy_views_) {
    gfx::Rect bounds = proxy_view->GetBounds();
    if (bounds.Contains(event.PositionInWidget().x,
                        event.PositionInWidget().y)) {
      blink::WebMouseWheelEvent proxy_event(event);
      proxy_event.SetPositionInWidget(
          proxy_event.PositionInWidget().x - bounds.x(),
          proxy_event.PositionInWidget().y - bounds.y());

      ui::MouseWheelEvent ui_event =
          UiMouseWheelEventFromWebMouseEvent(proxy_event);
      proxy_view->OnEvent(&ui_event);
      return;
    }
  }

  if (!IsPopupWidget() && popup_host_view_) {
    if (popup_host_view_->popup_position_.Contains(
            event.PositionInWidget().x, event.PositionInWidget().y)) {
      blink::WebMouseWheelEvent popup_mouse_wheel_event(event);
      popup_mouse_wheel_event.SetPositionInWidget(
          event.PositionInWidget().x - popup_host_view_->popup_position_.x(),
          event.PositionInWidget().y - popup_host_view_->popup_position_.y());
      popup_mouse_wheel_event.SetPositionInScreen(
          popup_mouse_wheel_event.PositionInWidget().x,
          popup_mouse_wheel_event.PositionInWidget().y);

      popup_host_view_->SendMouseWheelEvent(popup_mouse_wheel_event);
      return;
    } else {
      // Scrolling outside of the popup widget so destroy it.
      // Execute asynchronously to avoid deleting the widget from inside some
      // other callback.
      base::PostTask(
          FROM_HERE, {content::BrowserThread::UI},
          base::BindOnce(&OffScreenRenderWidgetHostView::CancelWidget,
                         popup_host_view_->weak_ptr_factory_.GetWeakPtr()));
    }
  }

  if (render_widget_host_ && render_widget_host_->GetView()) {
    blink::WebMouseWheelEvent& mouse_wheel_event =
        const_cast<blink::WebMouseWheelEvent&>(event);

    mouse_wheel_phase_handler_.SendWheelEndForTouchpadScrollingIfNeeded(false);
    mouse_wheel_phase_handler_.AddPhaseIfNeededAndScheduleEndEvent(
        mouse_wheel_event, false);

    if (ShouldRouteEvents()) {
      render_widget_host_->delegate()
          ->GetInputEventRouter()
          ->RouteMouseWheelEvent(
              this, const_cast<blink::WebMouseWheelEvent*>(&mouse_wheel_event),
              ui::LatencyInfo(ui::SourceEventType::WHEEL));
    } else {
      render_widget_host_->GetView()->ProcessMouseWheelEvent(
          mouse_wheel_event, ui::LatencyInfo(ui::SourceEventType::WHEEL));
    }
  }
}

bool OffScreenRenderWidgetHostView::ShouldRouteEvents() const {
  if (!render_widget_host_->delegate())
    return false;

  // Do not route events that are currently targeted to page popups such as
  // <select> element drop-downs, since these cannot contain cross-process
  // frames.
  if (!render_widget_host_->delegate()->IsWidgetForMainFrame(
          render_widget_host_)) {
    return false;
  }

  return !!render_widget_host_->delegate()->GetInputEventRouter();
}

void OffScreenRenderWidgetHostView::SetPainting(bool painting) {
  painting_ = painting;

  // time_source_->SetActive(painting_);
  // background_time_source_->SetActive(!painting_);

  if (popup_host_view_) {
    popup_host_view_->SetPainting(painting);
  }

  if (video_consumer_) {
    video_consumer_->SetActive(IsPainting());
  } else if (host_display_client_) {
    host_display_client_->SetActive(IsPainting());
  }

  Invalidate();
}

bool OffScreenRenderWidgetHostView::IsPainting() const {
  return painting_;
}

void OffScreenRenderWidgetHostView::SetFrameRate(int frame_rate) {
  if (parent_host_view_) {
    if (parent_host_view_->GetFrameRate() == GetFrameRate())
      return;

    frame_rate_ = parent_host_view_->GetFrameRate();
  } else {
    if (frame_rate <= 0)
      frame_rate = 1;
    if (frame_rate > 240)
      frame_rate = 240;

    frame_rate_ = frame_rate;
  }

  SetupFrameRate();

  if (video_consumer_) {
    video_consumer_->SetFrameRate(GetFrameRate());
  }
}

int OffScreenRenderWidgetHostView::GetFrameRate() const {
  return frame_rate_;
}

bool OffScreenRenderWidgetHostView::UsingAutoScaleFactor() {
  return manual_device_scale_factor_ == kAutoScaleFactor;
}

void OffScreenRenderWidgetHostView::SetManualScaleFactor(float scale_factor) {
  manual_device_scale_factor_ = scale_factor;
  ResizeRootLayer(true);
}

float OffScreenRenderWidgetHostView::GetScaleFactor() {
  if (!UsingAutoScaleFactor())
    return manual_device_scale_factor_;

  return current_device_scale_factor_;
}

ui::Compositor* OffScreenRenderWidgetHostView::GetCompositor() const {
  return compositor_.get();
}

ui::Layer* OffScreenRenderWidgetHostView::GetRootLayer() const {
  return root_layer_.get();
}

const viz::LocalSurfaceIdAllocation&
OffScreenRenderWidgetHostView::GetLocalSurfaceIdAllocation() const {
  return delegated_frame_host_allocation_;
}

content::DelegatedFrameHost*
OffScreenRenderWidgetHostView::GetDelegatedFrameHost() const {
  return delegated_frame_host_.get();
}

void OffScreenRenderWidgetHostView::SetupFrameRate() {
  // if (time_source_) {
  //   time_source_->SetTimebaseAndInterval(
  //     base::TimeTicks::Now(),
  //     TimeDeltaFromHz(frame_rate_));
  // }

  if (compositor_) {
    compositor_->SetDisplayVSyncParameters(base::TimeTicks::Now(),
                                           TimeDeltaFromHz(frame_rate_));
  }
}

void OffScreenRenderWidgetHostView::Invalidate() {
  GetCompositor()->ScheduleFullRedraw();
}

void OffScreenRenderWidgetHostView::ResizeRootLayer(bool force) {
  SetupFrameRate();

  display::Display display =
      display::Screen::GetScreen()->GetDisplayNearestView(GetNativeView());
  const float scaleFactor = display.device_scale_factor();
  const bool scaleFactorDidChange = (scaleFactor != GetScaleFactor());

  current_device_scale_factor_ = scaleFactor;

  gfx::Size size = GetViewBounds().size();
  if (!force && !scaleFactorDidChange &&
      size == GetRootLayer()->bounds().size())
    return;

  GetRootLayer()->SetBounds(gfx::Rect(size));

  const gfx::Size& size_in_pixels =
      gfx::ConvertSizeToPixel(GetScaleFactor(), size);

  if (compositor_) {
    compositor_allocator_.GenerateId();
    compositor_allocation_ =
        compositor_allocator_.GetCurrentLocalSurfaceIdAllocation();
    compositor_->SetScaleAndSize(GetScaleFactor(), size_in_pixels,
                                 compositor_allocation_);
  }

  delegated_frame_host_allocator_.GenerateId();
  delegated_frame_host_allocation_ =
      delegated_frame_host_allocator_.GetCurrentLocalSurfaceIdAllocation();

  GetDelegatedFrameHost()->EmbedSurface(
      delegated_frame_host_allocation_.local_surface_id(), size,
      cc::DeadlinePolicy::UseDefaultDeadline());

  // Note that |render_widget_host_| will retrieve resize parameters from the
  // DelegatedFrameHost, so it must have SynchronizeVisualProperties called
  // after.
  if (render_widget_host_) {
    render_widget_host_->SynchronizeVisualProperties();
  }
}

viz::FrameSinkId OffScreenRenderWidgetHostView::AllocateFrameSinkId() {
  return viz::FrameSinkId(
      base::checked_cast<uint32_t>(render_widget_host_->GetProcess()->GetID()),
      base::checked_cast<uint32_t>(render_widget_host_->GetRoutingID()));
}

void OffScreenRenderWidgetHostView::UpdateBackgroundColorFromRenderer(
    SkColor color) {
  if (color == background_color_)
    return;
  background_color_ = color;

  bool opaque = SkColorGetA(color) == SK_AlphaOPAQUE;
  GetRootLayer()->SetFillsBoundsOpaquely(opaque);
  GetRootLayer()->SetColor(color);
}

}  // namespace electron
