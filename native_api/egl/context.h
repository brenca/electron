// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NATIVE_API_EGL_CONTEXT_H_
#define NATIVE_API_EGL_CONTEXT_H_

#include <memory>

#include <EGL/egl.h>
#include "base/macros.h"

#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"

namespace gpu {
class ServiceDiscardableManager;
class TransferBuffer;

namespace gles2 {
class GLES2CmdHelper;
class GLES2Interface;
}  // namespace gles2
}  // namespace gpu

namespace egl {
class Display;
class Surface;
class Config;

class Context : public base::RefCountedThreadSafe<Context> {
 public:
  Context(Display* display, const Config* config);

  bool is_current_in_some_thread() const { return is_current_in_some_thread_; }
  void set_is_current_in_some_thread(bool flag) {
    is_current_in_some_thread_ = flag;
  }
  void MarkDestroyed();
  bool SwapBuffers(Surface* current_surface);

  static bool MakeCurrent(Context* current_context,
                          Surface* current_surface,
                          Context* new_context,
                          Surface* new_surface);

  static bool ValidateAttributeList(const EGLint* attrib_list);

  // Called by ThreadState to set the needed global variables when this context
  // is current.
  void ApplyCurrentContext();
  static void ApplyContextReleased();

 private:
  friend class base::RefCountedThreadSafe<Context>;
  ~Context();
  bool ConnectToService(Surface* surface);
  bool ConnectedToService() const;

  bool WasServiceContextLost() const;
  bool IsCompatibleSurface(Surface* surface) const;
  bool Flush();

  static gpu::GpuFeatureInfo platform_gpu_feature_info_;

  Display* display_;
  const Config* config_;
  bool is_current_in_some_thread_;
  bool is_destroyed_;
  bool should_set_draw_rectangle_;

  scoped_refptr<viz::ContextProviderCommandBuffer> context_provider_;

  DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace egl

#endif  // NATIVE_API_EGL_CONTEXT_H_
