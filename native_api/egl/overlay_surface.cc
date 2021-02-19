// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/native_api/egl/overlay_surface.h"

#include "components/viz/common/resources/resource_format.h"
#include "content/public/browser/gpu_utils.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/common/gpu_memory_buffer_support.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/geometry/rect_f.h"

#include "base/logging.h"

namespace egl {

OverlaySurface::OverlaySurface(
    scoped_refptr<viz::ContextProviderCommandBuffer> context_provider,
    gpu::SurfaceHandle surface_handle)
    : context_provider_(context_provider) {
  DCHECK(buffer_queue_);

  buffer_queue_ = std::make_unique<viz::BufferQueue>(
      context_provider->SharedImageInterface(), surface_handle);
  buffer_queue_->SetSyncTokenProvider(this);

  auto* gl = context_provider_->ContextGL();
  gl->GenFramebuffers(1, &fbo_);
  gl->FramebufferBackbuffer(fbo_);
  Reshape(gfx::Size(50,50), 1.0f);
  BindFramebuffer();
}

OverlaySurface::~OverlaySurface() {
  DCHECK_NE(0u, fbo_);
  auto* gl = context_provider_->ContextGL();
  gl->FramebufferBackbuffer(0);
  gl->DeleteFramebuffers(1, &fbo_);

  for (const auto& buffer_texture : buffer_queue_textures_)
    gl->DeleteTextures(1u, &buffer_texture.second);
  buffer_queue_textures_.clear();

  current_texture_ = 0u;
  last_bound_texture_ = 0u;
  last_bound_mailbox_.SetZero();

  // Freeing the BufferQueue here ensures that *this is fully alive in case the
  // BufferQueue needs the SyncTokenProvider functionality.
  buffer_queue_.reset();
  fbo_ = 0u;
}

gpu::SyncToken OverlaySurface::GenSyncToken() {
  DCHECK(fbo_);
  gpu::SyncToken sync_token;
  context_provider_->ContextGL()->GenUnverifiedSyncTokenCHROMIUM(
      sync_token.GetData());
  return sync_token;
}

void OverlaySurface::Reshape(const gfx::Size& size, float device_scale_factor) {
  size_ = size;
  scale_ = device_scale_factor;
  gfx::ColorSpace color_space = gfx::ColorSpace::CreateSRGB();
  gfx::BufferFormat format = gfx::BufferFormat::RGBA_8888;

  auto* gl = context_provider_->ContextGL();

  gl->ResizeCHROMIUM(size.width(), size.height(), device_scale_factor,
                     color_space.AsGLColorSpace(), true);

  DCHECK(buffer_queue_);
  const bool may_have_freed_buffers =
      buffer_queue_->Reshape(size, color_space, format);
  if (may_have_freed_buffers) {
    gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Note that |texture_target_| is initially set to 0, and so if it has not
    // been set to a valid value, then no buffers have been allocated.
    if (texture_target_ && may_have_freed_buffers) {
      gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               texture_target_, 0, 0);
      for (const auto& buffer_texture : buffer_queue_textures_)
        gl->DeleteTextures(1u, &buffer_texture.second);
      buffer_queue_textures_.clear();
      current_texture_ = 0u;
      last_bound_texture_ = 0u;
      last_bound_mailbox_.SetZero();
    }
  }

  texture_target_ =
      gpu::GetBufferTextureTarget(gfx::BufferUsage::SCANOUT, format,
                                  context_provider_->ContextCapabilities());
}

void OverlaySurface::SwapBuffers() {
  DCHECK(buffer_queue_);

  GLfloat opacity = 1.0f;
  GLfloat contents_rect[4] = {0, 0, 1.0f, 1.0f};
  GLfloat bounds_rect[4] = {0, 0, size_.width(), size_.height()};
  GLboolean is_clipped = GL_FALSE;
  GLfloat clip_rect[4] = {0, 0, 0, 0};
  GLfloat rounded_corner_bounds[5] = {0, 0, 0, 0, 0};
  GLint sorting_context_id = 0;
  GLfloat transform[16];
  SkMatrix44(SkMatrix44::kIdentity_Constructor).asColMajorf(transform);
  unsigned filter = GL_NEAREST;
  unsigned edge_aa_mask = 0;

  context_provider_->ContextGL()->ScheduleCALayerSharedStateCHROMIUM(
      opacity, is_clipped, clip_rect, rounded_corner_bounds, sorting_context_id,
      transform);

  context_provider_->ContextGL()->ScheduleCALayerCHROMIUM(
      current_texture_, contents_rect, SK_ColorBLACK, edge_aa_mask, bounds_rect,
      filter);

  if (current_texture_) {
    auto* gl = context_provider_->ContextGL();
    gl->EndSharedImageAccessDirectCHROMIUM(current_texture_);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0u);
    current_texture_ = 0u;
  }

  buffer_queue_->SwapBuffers(gfx::Rect(size_));
  BindFramebuffer();
}

void OverlaySurface::SwapBuffersComplete() {
  buffer_queue_->PageFlipComplete();
}

void OverlaySurface::BindFramebuffer() {
  auto* gl = context_provider_->ContextGL();
  gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);

  if (current_texture_)
    return;

  DCHECK(buffer_queue_);
  gpu::SyncToken creation_sync_token;
  const gpu::Mailbox current_buffer =
      buffer_queue_->GetCurrentBuffer(&creation_sync_token);
  if (current_buffer.IsZero())
    return;
  gl->WaitSyncTokenCHROMIUM(creation_sync_token.GetConstData());
  unsigned& buffer_texture = buffer_queue_textures_[current_buffer];
  if (!buffer_texture) {
    buffer_texture =
        gl->CreateAndTexStorage2DSharedImageCHROMIUM(current_buffer.name);
  }
  current_texture_ = buffer_texture;
  gl->BeginSharedImageAccessDirectCHROMIUM(
      current_texture_, GL_SHARED_IMAGE_ACCESS_MODE_READWRITE_CHROMIUM);
  gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           texture_target_, current_texture_, 0);
  last_bound_texture_ = current_texture_;
  last_bound_mailbox_ = current_buffer;
}

}  // namespace egl
