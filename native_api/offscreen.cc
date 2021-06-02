// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/native_api/offscreen.h"

#include <map>

#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/common/gin_helper/trackable_object.h"

#if defined(OS_WIN)
#include <windows.h>

#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/ipc/common/gpu_memory_buffer_impl_dxgi.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "native_api/egl/context.h"
#include "native_api/egl/thread_state.h"
#endif

namespace {
electron::api::gpu::Mailbox ApiMailboxFromGpuMailbox(::gpu::Mailbox mailbox) {
  electron::api::gpu::Mailbox api_mailbox;

  memcpy(api_mailbox.name, mailbox.name, 16);
  api_mailbox.shared_image = mailbox.IsSharedImage();

  return api_mailbox;
}

::gpu::Mailbox GpuMailboxFromApiMailbox(electron::api::gpu::Mailbox mailbox) {
  ::gpu::Mailbox gpu_mailbox;

  memcpy(gpu_mailbox.name, mailbox.name, 16);

  return gpu_mailbox;
}
}  // namespace

namespace electron {
namespace api {
namespace offscreen {

class WCPaintObserver : public WebContents::PaintObserver {
 public:
  WCPaintObserver(offscreen::PaintObserver* observer) : observer_(observer) {
    map_[observer_] = this;
  }

  ~WCPaintObserver() override { map_.erase(observer_); }

  static WCPaintObserver* fromPaintObserver(
      offscreen::PaintObserver* observer) {
    return map_.at(observer);
  }

  void OnPaint(const gfx::Rect& dirty_rect, const SkBitmap& bitmap) override {
    if (observer_ != nullptr) {
      observer_->OnPaint(dirty_rect.x(), dirty_rect.y(), dirty_rect.width(),
                         dirty_rect.height(), bitmap.width(), bitmap.height(),
                         bitmap.getPixels());
    }
  }

  void OnTexturePaint(const ::gpu::Mailbox& mailbox,
                      const ::gpu::SyncToken& sync_token,
                      const gfx::Rect& content_rect,
                      bool is_popup,
                      void (*callback)(void*, void*),
                      void* context) override {
    if (observer_ != nullptr) {
      electron::api::gpu::Mailbox api_mailbox =
          ApiMailboxFromGpuMailbox(mailbox);

      electron::api::gpu::SyncToken api_sync_token;
      api_sync_token.verified_flush = sync_token.verified_flush();
      api_sync_token.namespace_id =
          (electron::api::gpu::CommandBufferNamespace)sync_token.namespace_id();
      api_sync_token.command_buffer_id =
          sync_token.command_buffer_id().GetUnsafeValue();
      api_sync_token.release_count = sync_token.release_count();

      observer_->OnTexturePaint(api_mailbox, api_sync_token, content_rect.x(),
                                content_rect.y(), content_rect.width(),
                                content_rect.height(), is_popup, callback,
                                context);
    }
  }

 private:
  offscreen::PaintObserver* observer_;

  static std::map<offscreen::PaintObserver*, WCPaintObserver*> map_;
};

std::map<offscreen::PaintObserver*, WCPaintObserver*> WCPaintObserver::map_ =
    {};

ELECTRON_EXTERN void __cdecl addPaintObserver(int id, PaintObserver* observer) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();

  auto* obs = new WCPaintObserver(observer);
  auto* web_contents =
      gin_helper::TrackableObject<WebContents>::FromWeakMapID(isolate, id);

  web_contents->AddPaintObserver(obs);
}

ELECTRON_EXTERN void __cdecl removePaintObserver(int id,
                                                 PaintObserver* observer) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();

  auto* obs = WCPaintObserver::fromPaintObserver(observer);
  auto* web_contents =
      gin_helper::TrackableObject<WebContents>::FromWeakMapID(isolate, id);

  web_contents->RemovePaintObserver(obs);
}

ELECTRON_EXTERN electron::api::gpu::Mailbox __cdecl
createMailboxFromD3D11SharedHandle(void* handle, int width, int height) {
#if defined(OS_WIN)
  egl::ThreadState* ts = egl::ThreadState::Get();
  egl::Context* context = ts->current_context();

  if (context) {
    gfx::GpuMemoryBufferHandle gpu_memory_buffer_handle;
    gpu_memory_buffer_handle.dxgi_handle.Set(HANDLE(handle));
    gpu_memory_buffer_handle.type = gfx::DXGI_SHARED_HANDLE;

    std::unique_ptr<::gpu::GpuMemoryBufferImplDXGI> gpu_memory_buffer =
        ::gpu::GpuMemoryBufferImplDXGI::CreateFromHandle(
            std::move(gpu_memory_buffer_handle), gfx::Size(width, height),
            gfx::BufferFormat::RGBA_8888, gfx::BufferUsage::GPU_READ,
            base::DoNothing());

    const uint32_t shared_image_usage =
        ::gpu::SharedImageUsage::SHARED_IMAGE_USAGE_DISPLAY |
        ::gpu::SharedImageUsage::SHARED_IMAGE_USAGE_GLES2;

    return ApiMailboxFromGpuMailbox(context->CreateSharedImage(
        gpu_memory_buffer.get(), gfx::ColorSpace::CreateSRGB(),
        shared_image_usage));
  } else {
    return electron::api::gpu::Mailbox();
  }
#else
  return electron::api::gpu::Mailbox();
#endif
}

ELECTRON_EXTERN void __cdecl
releaseMailbox(electron::api::gpu::Mailbox mailbox) {
#if defined(OS_WIN)
  egl::ThreadState* ts = egl::ThreadState::Get();
  egl::Context* context = ts->current_context();

  if (context) {
    context->DeleteSharedImage(GpuMailboxFromApiMailbox(mailbox));
  }
#endif
}

}  // namespace offscreen
}  // namespace api
}  // namespace electron
