// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/native_api/offscreen.h"

#include <map>

#include "shell/browser/api/electron_api_web_contents.h"

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
      electron::api::gpu::Mailbox api_mailbox;
      memcpy(api_mailbox.name, mailbox.name, 16);
      api_mailbox.shared_image = mailbox.IsSharedImage();

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
      mate::TrackableObject<WebContents>::FromWeakMapID(isolate, id);

  web_contents->AddPaintObserver(obs);
}

ELECTRON_EXTERN void __cdecl removePaintObserver(int id,
                                                 PaintObserver* observer) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();

  auto* obs = WCPaintObserver::fromPaintObserver(observer);
  auto* web_contents =
      mate::TrackableObject<WebContents>::FromWeakMapID(isolate, id);

  web_contents->RemovePaintObserver(obs);
}

}  // namespace offscreen
}  // namespace api
}  // namespace electron
