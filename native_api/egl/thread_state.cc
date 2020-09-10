// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/native_api/egl/thread_state.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/lazy_instance.h"
#include "base/message_loop/message_pump_type.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "electron/native_api/egl/context.h"
#include "electron/native_api/egl/display.h"
#include "electron/native_api/egl/surface.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "gpu/command_buffer/common/thread_local.h"
#include "gpu/config/gpu_info_collector.h"
#include "gpu/config/gpu_preferences.h"
#include "gpu/config/gpu_util.h"

#include "content/public/browser/browser_thread.h"
#include "content/public/browser/gpu_utils.h"

// Thread local key for ThreadState instance. Accessed when holding g_egl_lock
// only, since the initialization can not be Guaranteed otherwise.  Not in
// anonymous namespace due to Mac OS X 10.6 linker. See gles2_lib.cc.
static gpu::ThreadLocalKey g_egl_thread_state_key;

namespace {
base::LazyInstance<base::Lock>::Leaky g_egl_lock;
int g_egl_active_thread_count;

egl::Display* g_egl_default_display;
}  // namespace

namespace egl {

egl::ThreadState* ThreadState::Get() {
  base::AutoLock lock(g_egl_lock.Get());
  if (g_egl_active_thread_count == 0) {
    // if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    //   LOG(INFO) << "Not on browser thread :((" << '\n';
    //   return nullptr;
    // }

    if (!content::GetGpuChannelEstablishFactory()) {
      return nullptr;
    }

    gles2::Initialize();

    g_egl_default_display = new egl::Display();
    g_egl_thread_state_key = gpu::ThreadLocalAlloc();
  }
  egl::ThreadState* thread_state = static_cast<egl::ThreadState*>(
      gpu::ThreadLocalGetValue(g_egl_thread_state_key));
  if (!thread_state) {
    thread_state = new egl::ThreadState;
    gpu::ThreadLocalSetValue(g_egl_thread_state_key, thread_state);
    ++g_egl_active_thread_count;
  }
  return thread_state;
}

void ThreadState::ReleaseThread() {
  base::AutoLock lock(g_egl_lock.Get());
  if (g_egl_active_thread_count == 0)
    return;

  egl::ThreadState* thread_state = static_cast<egl::ThreadState*>(
      gpu::ThreadLocalGetValue(g_egl_thread_state_key));
  if (!thread_state)
    return;

  --g_egl_active_thread_count;
  if (g_egl_active_thread_count > 0) {
    g_egl_default_display->ReleaseCurrent(thread_state);
    delete thread_state;
  } else {
    gpu::ThreadLocalFree(g_egl_thread_state_key);

    // First delete the display object, so that it drops the possible refs to
    // current context.
    delete g_egl_default_display;
    g_egl_default_display = nullptr;

    // We can use Surface and Context without lock, since there's no threads
    // left anymore. Destroy the current context explicitly, in an attempt to
    // reduce the number of error messages abandoned context would produce.
    if (thread_state->current_context()) {
      Context::MakeCurrent(thread_state->current_context(),
                           thread_state->current_surface(), nullptr, nullptr);
    }
    delete thread_state;

    gles2::Terminate();
  }
}

ThreadState::ThreadState() : error_code_(EGL_SUCCESS) {
  if (!base::ThreadTaskRunnerHandle::IsSet()) {
    task_executor_.reset(
        new base::SingleThreadTaskExecutor(base::MessagePumpType::DEFAULT));
  }
}

ThreadState::~ThreadState() = default;

EGLint ThreadState::ConsumeErrorCode() {
  EGLint current_error_code = error_code_;
  error_code_ = EGL_SUCCESS;
  return current_error_code;
}

Display* ThreadState::GetDisplay(EGLDisplay dpy) {
  if (dpy == g_egl_default_display)
    return g_egl_default_display;
  return nullptr;
}

Display* ThreadState::GetDefaultDisplay() {
  return g_egl_default_display;
}

void ThreadState::SetCurrent(Surface* surface, Context* context) {
  DCHECK((surface == nullptr) == (context == nullptr));
  if (current_context_) {
    current_context_->set_is_current_in_some_thread(false);
    current_surface_->set_is_current_in_some_thread(false);
  }
  current_surface_ = surface;
  current_context_ = context;
  if (current_context_) {
    current_context_->set_is_current_in_some_thread(true);
    current_surface_->set_is_current_in_some_thread(true);
  }
}

ThreadState::AutoCurrentContextRestore::AutoCurrentContextRestore(
    ThreadState* thread_state)
    : thread_state_(thread_state) {}

ThreadState::AutoCurrentContextRestore::~AutoCurrentContextRestore() {
  if (Context* current_context = thread_state_->current_context()) {
    current_context->ApplyCurrentContext();
  } else {
    Context::ApplyContextReleased();
  }
}

void ThreadState::AutoCurrentContextRestore::SetCurrent(Surface* surface,
                                                        Context* context) {
  thread_state_->SetCurrent(surface, context);
}

}  // namespace egl
