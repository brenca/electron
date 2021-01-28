// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_OSR_OSR_RENDER_WIDGET_HOST_VIEW_H_
#define SHELL_BROWSER_OSR_OSR_RENDER_WIDGET_HOST_VIEW_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#if defined(OS_WIN)
#include <windows.h>
#endif

#include "base/process/kill.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "components/viz/common/surfaces/parent_local_surface_id_allocator.h"
#include "content/browser/renderer_host/delegated_frame_host.h"  // nogncheck
#include "content/browser/renderer_host/input/mouse_wheel_phase_handler.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_impl.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_view_base.h"  // nogncheck
#include "content/browser/web_contents/web_contents_view.h"  // nogncheck
#include "shell/browser/osr/osr_host_display_client.h"
#include "shell/browser/osr/osr_video_consumer.h"
#include "shell/browser/osr/osr_view_proxy.h"
#include "third_party/blink/public/platform/web_vector.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer_owner.h"
#include "ui/gfx/geometry/point.h"

#include "components/viz/host/host_display_client.h"

#if defined(OS_WIN)
#include "ui/gfx/win/window_impl.h"
#endif

namespace content {
class CursorManager;
}  // namespace content

namespace electron {

class ElectronDelegatedFrameHostClient;

typedef base::Callback<void(const gfx::Rect&, const SkBitmap&)> OnPaintCallback;
typedef base::Callback<void(const gpu::Mailbox&,
                            const gpu::SyncToken&,
                            const gfx::Rect&,
                            bool,
                            void (*)(void*, void*),
                            void*)>
    OnTexturePaintCallback;
typedef base::Callback<void(const gfx::Rect&)> OnPopupPaintCallback;
typedef base::Callback<void(const gpu::Mailbox&,
                            const gpu::SyncToken&,
                            const gfx::Rect&,
                            void (*)(void*, void*),
                            void*)>
    OnPopupTexturePaintCallback;

class OffScreenRenderWidgetHostView : public content::RenderWidgetHostViewBase,
                                      public ui::CompositorDelegate,
                                      public viz::DelayBasedTimeSourceClient,
                                      public OffscreenViewProxyObserver {
 public:
  class Initializer {
   public:
    virtual bool IsTransparent() const = 0;
    virtual const OnPaintCallback& GetPaintCallback() const = 0;
    virtual const OnTexturePaintCallback& GetTexturePaintCallback() const = 0;
    virtual gfx::Size GetInitialSize() const = 0;
  };

  OffScreenRenderWidgetHostView(Initializer* initializer,
                                content::RenderWidgetHost* host,
                                OffScreenRenderWidgetHostView* parent,
                                bool painting,
                                int frame_rate,
                                float scale_factor);
  ~OffScreenRenderWidgetHostView() override;

  content::BrowserAccessibilityManager* CreateBrowserAccessibilityManager(
      content::BrowserAccessibilityDelegate*,
      bool) override;

  // viz::DelayBasedTimeSourceClient:
  void OnTimerTick() override;
  void OnFrameAck(const viz::BeginFrameAck& ack);

  // content::RenderWidgetHostView:
  void InitAsChild(gfx::NativeView) override;
  void SetSize(const gfx::Size&) override;
  void SetBounds(const gfx::Rect&) override;
  gfx::NativeView GetNativeView() override;
  gfx::NativeViewAccessible GetNativeViewAccessible() override;
  void Focus() override;
  bool HasFocus() override;
  uint32_t GetCaptureSequenceNumber() const override;
  bool IsSurfaceAvailableForCopy() override;
  void Show() override;
  void Hide() override;
  bool IsShowing() override;
  void EnsureSurfaceSynchronizedForWebTest() override;
  gfx::Rect GetViewBounds() override;
  void SetBackgroundColor(SkColor color) override;
  base::Optional<SkColor> GetBackgroundColor() override;
  void UpdateBackgroundColor() override;
  blink::mojom::PointerLockResult LockMouse(
      bool request_unadjusted_movement) override;
  blink::mojom::PointerLockResult ChangeMouseLock(
      bool request_unadjusted_movement) override;
  void UnlockMouse() override;
  void TakeFallbackContentFrom(content::RenderWidgetHostView* view) override;
#if defined(OS_MACOSX)
  void SetActive(bool active) override;
  void ShowDefinitionForSelection() override;
  void SpeakSelection() override;
  bool UpdateNSViewAndDisplay();
#endif  // defined(OS_MACOSX)

  // content::RenderWidgetHostViewBase:
  void ResetFallbackToFirstNavigationSurface() override;
  void InitAsPopup(content::RenderWidgetHostView* rwhv,
                   const gfx::Rect& rect) override;
  void InitAsFullscreen(content::RenderWidgetHostView*) override;
  void UpdateCursor(const content::WebCursor&) override;
  void SetIsLoading(bool is_loading) override;
  void RenderProcessGone() override;
  void Destroy() override;
  void SetTooltipText(const base::string16&) override;
  content::CursorManager* GetCursorManager() override;
  gfx::Size GetCompositorViewportPixelSize() override;

  content::RenderWidgetHostViewBase* CreateViewForWidget(
      content::RenderWidgetHost*,
      content::RenderWidgetHost*,
      content::WebContentsView*) override;

  void CopyFromSurface(
      const gfx::Rect& src_rect,
      const gfx::Size& output_size,
      base::OnceCallback<void(const SkBitmap&)> callback) override;
  void GetScreenInfo(content::ScreenInfo* results) override;
  void TransformPointToRootSurface(gfx::PointF* point) override;
  gfx::Rect GetBoundsInRootWindow() override;

#if !defined(OS_MAC)
  viz::ScopedSurfaceIdAllocator DidUpdateVisualProperties(
      const cc::RenderFrameMetadata& metadata) override;
#endif

  viz::SurfaceId GetCurrentSurfaceId() const override;
  void ImeCompositionRangeChanged(const gfx::Range&,
                                  const std::vector<gfx::Rect>&) override;
  std::unique_ptr<content::SyntheticGestureTarget>
  CreateSyntheticGestureTarget() override;
  bool TransformPointToCoordSpaceForView(
      const gfx::PointF& point,
      RenderWidgetHostViewBase* target_view,
      gfx::PointF* transformed_point) override;
  void DidNavigate() override;
  const viz::LocalSurfaceIdAllocation& GetLocalSurfaceIdAllocation()
      const override;
  const viz::FrameSinkId& GetFrameSinkId() const override;
  viz::FrameSinkId GetRootFrameSinkId() override;

  // ui::CompositorDelegate:
  std::unique_ptr<viz::HostDisplayClient> CreateHostDisplayClient(
      ui::Compositor* compositor) override;

  bool InstallTransparency();
  void WasResized();
  void SynchronizeVisualProperties(
      const cc::DeadlinePolicy& deadline_policy,
      const base::Optional<viz::LocalSurfaceIdAllocation>&
          child_local_surface_id_allocation);
  void Invalidate();
  gfx::Size SizeInPixels();

  void SendMouseEvent(const blink::WebMouseEvent& event);
  void SendMouseWheelEvent(const blink::WebMouseWheelEvent& event);
  bool ShouldRouteEvents() const;

  void OnPaint(const gfx::Rect& damage_rect, const SkBitmap& bitmap);
  void OnPopupTexturePaint(const gpu::Mailbox& mailbox,
                           const gpu::SyncToken& sync_token,
                           const gfx::Rect& content_rect,
                           void (*callback)(void*, void*),
                           void* context);
  void OnTexturePaint(const gpu::Mailbox& mailbox,
                      const gpu::SyncToken& sync_token,
                      const gfx::Rect& content_rect,
                      void (*callback)(void*, void*),
                      void* context);
  void OnPopupPaint(const gfx::Rect& damage_rect);
  void OnProxyViewPaint(const gfx::Rect& damage_rect) override;
  void CompositeFrame(const gfx::Rect& damage_rect);

  void CancelWidget();

  void AddViewProxy(OffscreenViewProxy* proxy);
  void RemoveViewProxy(OffscreenViewProxy* proxy);
  void ProxyViewDestroyed(OffscreenViewProxy* proxy) override;

  bool IsPopupWidget() const {
    return widget_type_ == content::WidgetType::kPopup;
  }

  const SkBitmap& GetBacking() { return *backing_.get(); }

  void SetPainting(bool painting);
  bool IsPainting() const;

  void SetFrameRate(int frame_rate);
  int GetFrameRate() const;

  bool UsingAutoScaleFactor();
  void SetManualScaleFactor(float scale_factor);
  float GetScaleFactor();

  void OnDidUpdateVisualPropertiesComplete(
      const cc::RenderFrameMetadata& metadata);

  ui::Compositor* GetCompositor() const;
  ui::Layer* GetRootLayer() const;

  content::DelegatedFrameHost* GetDelegatedFrameHost() const;

  content::RenderWidgetHostImpl* render_widget_host() const {
    return render_widget_host_;
  }

  gfx::Size size() const { return size_; }

  void set_popup_host_view(OffScreenRenderWidgetHostView* popup_view) {
    popup_host_view_ = popup_view;
  }

  void set_child_host_view(OffScreenRenderWidgetHostView* child_view) {
    child_host_view_ = child_view;
  }

 private:
  void SetupFrameRate();
  bool SetRootLayerSize(bool force);

  bool ResizeRootLayer();
  void ReleaseResizeHold();

  viz::FrameSinkId AllocateFrameSinkId();

  // Forces the view to allocate a new viz::LocalSurfaceId for the next
  // CompositorFrame submission in anticipation of a synchronization operation
  // that does not involve a resize or a device scale factor change.
  void AllocateLocalSurfaceId();
  const viz::LocalSurfaceIdAllocation& GetCurrentLocalSurfaceIdAllocation() const;

  // Sets the current viz::LocalSurfaceId, in cases where the embedded client
  // has allocated one. Also sets child sequence number component of the
  // viz::LocalSurfaceId allocator.
  void UpdateLocalSurfaceIdFromEmbeddedClient(
      const base::Optional<viz::LocalSurfaceIdAllocation>&
          local_surface_id_allocation);

  // Returns the current viz::LocalSurfaceIdAllocation.
  const viz::LocalSurfaceIdAllocation& GetOrCreateLocalSurfaceIdAllocation();

  // Marks the current viz::LocalSurfaceId as invalid. AllocateLocalSurfaceId
  // must be called before submitting new CompositorFrames.
  void InvalidateLocalSurfaceId();

  // Applies background color without notifying the RenderWidget about
  // opaqueness changes.
  void UpdateBackgroundColorFromRenderer(SkColor color);

  SkColor background_color_ = SkColor();

  int frame_rate_ = 0;
  float manual_device_scale_factor_;

  std::unique_ptr<ui::Layer> root_layer_;
  std::unique_ptr<ui::Compositor> compositor_;
  std::unique_ptr<content::DelegatedFrameHost> delegated_frame_host_;
  std::unique_ptr<ElectronDelegatedFrameHostClient>
      delegated_frame_host_client_;

  // Used to allocate LocalSurfaceIds when this is embedding external content.
  std::unique_ptr<viz::ParentLocalSurfaceIdAllocator>
      parent_local_surface_id_allocator_;
  viz::ParentLocalSurfaceIdAllocator compositor_local_surface_id_allocator_;

  std::unique_ptr<content::CursorManager> cursor_manager_;

  // Provides |source_id| for BeginFrameArgs that we create.
  viz::StubBeginFrameSource begin_frame_source_;
  std::unique_ptr<viz::DelayBasedTimeSource> time_source_;
  std::unique_ptr<viz::DelayBasedTimeSource> background_time_source_;
  bool can_send_frame_ = true;
  uint64_t begin_frame_sequence_number_ =
      viz::BeginFrameArgs::kStartingFrameNumber;

  OffScreenHostDisplayClient* host_display_client_;
  std::unique_ptr<OffScreenVideoConsumer> video_consumer_;

  bool hold_resize_ = false;
  bool pending_resize_ = false;

  // The associated Model.  While |this| is being Destroyed,
  // |render_widget_host_| is NULL and the message loop is run one last time
  // Message handlers must check for a NULL |render_widget_host_|.
  content::RenderWidgetHostImpl* render_widget_host_;

  OffScreenRenderWidgetHostView* parent_host_view_ = nullptr;
  OffScreenRenderWidgetHostView* popup_host_view_ = nullptr;
  OffScreenRenderWidgetHostView* child_host_view_ = nullptr;
  std::set<OffscreenViewProxy*> proxy_views_;

  OnPaintCallback callback_;
  OnTexturePaintCallback texture_callback_;
  OnPopupPaintCallback parent_callback_;
  OnPopupTexturePaintCallback parent_texture_callback_;
  bool paint_callback_running_ = false;
  std::unique_ptr<SkBitmap> backing_;

  bool transparent_;
  bool painting_;
  bool is_showing_ = false;
  bool is_first_navigation_ = true;
  bool is_destroyed_ = false;

  gfx::Size size_;
  gfx::Rect popup_position_;

  content::MouseWheelPhaseHandler mouse_wheel_phase_handler_;

  // Latest capture sequence number which is incremented when the caller
  // requests surfaces be synchronized via
  // EnsureSurfaceSynchronizedForWebTest().
  uint32_t latest_capture_sequence_number_ = 0u;

  base::WeakPtrFactory<OffScreenRenderWidgetHostView> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(OffScreenRenderWidgetHostView);
};

}  // namespace electron

#endif  // SHELL_BROWSER_OSR_OSR_RENDER_WIDGET_HOST_VIEW_H_
