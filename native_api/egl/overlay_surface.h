// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NATIVE_API_EGL_OVERLAY_SURFACE_H_
#define NATIVE_API_EGL_OVERLAY_SURFACE_H_

#include "base/containers/flat_map.h"
#include "base/memory/weak_ptr.h"
#include "components/viz/service/display_embedder/buffer_queue.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/swap_result.h"

namespace egl {

class OverlaySurface : public viz::BufferQueue::SyncTokenProvider {
 public:
  OverlaySurface(
      scoped_refptr<viz::ContextProviderCommandBuffer> context_provider,
      gpu::SurfaceHandle surface_handle);

  ~OverlaySurface() override;

  // viz::BufferQueue::SyncTokenProvider
  gpu::SyncToken GenSyncToken() override;

  void Reshape(const gfx::Size& size, float device_scale_factor);

  void SwapBuffers();
  void SwapBuffersComplete();
  void BindFramebuffer();

 private:
  scoped_refptr<viz::ContextProviderCommandBuffer> context_provider_;

  std::unique_ptr<viz::BufferQueue> buffer_queue_;
  base::flat_map<gpu::Mailbox, unsigned> buffer_queue_textures_;

  unsigned current_texture_ = 0u;
  unsigned last_bound_texture_ = 0u;
  gpu::Mailbox last_bound_mailbox_;
  unsigned texture_target_ = 0u;

  unsigned fbo_ = 0u;

  gfx::Size size_;
  float scale_;

  DISALLOW_COPY_AND_ASSIGN(OverlaySurface);
};

}  // namespace egl

#endif  // NATIVE_API_EGL_OVERLAY_SURFACE_H_
