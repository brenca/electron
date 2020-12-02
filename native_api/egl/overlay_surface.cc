// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/native_api/egl/overlay_surface.h"

#include "components/viz/common/resources/resource_format.h"
#include "content/public/browser/gpu_utils.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gl/color_space_utils.h"

namespace egl {

OverlaySurface::OverlaySurface(
    scoped_refptr<viz::ContextProviderCommandBuffer> context_provider,
    gpu::SurfaceHandle surface_handle)
    : context_provider_(context_provider), surface_handle_(surface_handle) {
  DCHECK(buffer_queue_);

  gpu::GpuChannelEstablishFactory* factory =
      content::GetGpuChannelEstablishFactory();
  buffer_queue_ = std::make_unique<viz::BufferQueue>(
      context_provider_->ContextGL(), gfx::BufferFormat::RGBA_8888,
      factory->GetGpuMemoryBufferManager(), surface_handle_,
      context_provider_->ContextCapabilities());
  auto* gl = context_provider_->ContextGL();
  gl->GenFramebuffers(1, &fbo_);
  gl->FramebufferBackbuffer(fbo_);
  BindFramebuffer();
}

OverlaySurface::~OverlaySurface() {
  DCHECK_NE(0u, fbo_);
  auto* gl = context_provider_->ContextGL();
  gl->FramebufferBackbuffer(0);
  gl->DeleteFramebuffers(1, &fbo_);
}

void OverlaySurface::Reshape(const gfx::Size& size, float device_scale_factor) {
  size_ = size;
  gfx::ColorSpace color_space = gfx::ColorSpace::CreateSRGB();
  auto* gl = context_provider_->ContextGL();

  gl->ResizeCHROMIUM(size.width(), size.height(), device_scale_factor,
                     gl::ColorSpaceUtils::GetGLColorSpace(color_space), true);

  DCHECK(buffer_queue_);
  buffer_queue_->Reshape(size, device_scale_factor, color_space, false);
}

void OverlaySurface::SwapBuffers() {
  DCHECK(buffer_queue_);

  unsigned stencil;
  current_texture_ = buffer_queue_->GetCurrentBuffer(&stencil);

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

  buffer_queue_->SwapBuffers(gfx::Rect(size_));
  BindFramebuffer();
}

void OverlaySurface::SwapBuffersComplete() {
  buffer_queue_->PageFlipComplete();
}

void OverlaySurface::BindFramebuffer() {
  unsigned stencil;
  if (current_texture_ == buffer_queue_->GetCurrentBuffer(&stencil))
    return;

  auto* gl = context_provider_->ContextGL();
  gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  DCHECK(buffer_queue_);

  current_texture_ = buffer_queue_->GetCurrentBuffer(&stencil);
  if (!current_texture_)
    return;

  gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           buffer_queue_->texture_target(), current_texture_,
                           0);
}

}  // namespace egl
