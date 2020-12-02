// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef NATIVE_API_WINDOW_H_
#define NATIVE_API_WINDOW_H_

#include <functional>

#include "electron/native_api/electron.h"

namespace electron {
namespace api {
namespace gpu {

enum CommandBufferNamespace : int8_t {
  INVALID = -1,

  GPU_IO,
  IN_PROCESS,
  VIZ_SKIA_OUTPUT_SURFACE,
  VIZ_SKIA_OUTPUT_SURFACE_NON_DDL,

  NUM_COMMAND_BUFFER_NAMESPACES
};

struct SyncToken {
  bool verified_flush;
  CommandBufferNamespace namespace_id;
  uint64_t command_buffer_id;
  uint64_t release_count;

  int8_t* GetData() { return reinterpret_cast<int8_t*>(this); }

  const int8_t* GetConstData() const {
    return reinterpret_cast<const int8_t*>(this);
  }
};

struct Mailbox {
  int8_t name[16];
  bool shared_image;

  bool operator<(const Mailbox& other) const {
    return memcmp(this, &other, sizeof other) < 0;
  }
  bool operator==(const Mailbox& other) const {
    return memcmp(this, &other, sizeof other) == 0;
  }
  bool operator!=(const Mailbox& other) const { return !operator==(other); }
};

}  // namespace gpu

namespace offscreen {

class ELECTRON_EXTERN PaintObserver {
 public:
  virtual void OnPaint(int dirty_x,
                       int dirty_y,
                       int dirty_width,
                       int dirty_height,
                       int bitmap_width,
                       int bitmap_height,
                       void* data) = 0;

  virtual void OnTexturePaint(const electron::api::gpu::Mailbox& mailbox,
                              const electron::api::gpu::SyncToken& sync_token,
                              int x,
                              int y,
                              int width,
                              int height,
                              bool is_popup,
                              void (*callback)(void*, void*),
                              void* context) = 0;
};

ELECTRON_EXTERN void __cdecl addPaintObserver(int id, PaintObserver* observer);
ELECTRON_EXTERN void __cdecl removePaintObserver(int id,
                                                 PaintObserver* observer);

}  // namespace offscreen
}  // namespace api
}  // namespace electron

#endif  // NATIVE_API_WINDOW_H_
