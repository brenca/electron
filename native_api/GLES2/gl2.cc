// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <GLES2/gl2.h>
#include <stdint.h>

#include "electron/native_api/electron.h"
#include "gpu/command_buffer/client/gles2_lib.h"

#include "base/logging.h"

extern "C" {
// These are defined in gl2extchromium.h, but have no GLES2 mapping
ELECTRON_EXTERN void glWaitSyncTokenCHROMIUM(const GLbyte* sync_token) {
  gles2::GetGLContext()->WaitSyncTokenCHROMIUM(sync_token);
}

ELECTRON_EXTERN void glGenSyncTokenCHROMIUM(GLbyte* sync_token) {
  // LOG(INFO) << "glGenSyncTokenCHROMIUM" << '\n';
  gles2::GetGLContext()->GenSyncTokenCHROMIUM(sync_token);
}

ELECTRON_EXTERN void glGenUnverifiedSyncTokenCHROMIUM(GLbyte* sync_token) {
  gles2::GetGLContext()->GenUnverifiedSyncTokenCHROMIUM(sync_token);
}

ELECTRON_EXTERN void glVerifySyncTokensCHROMIUM(GLbyte** sync_tokens,
                                                GLsizei count) {
  gles2::GetGLContext()->VerifySyncTokensCHROMIUM(sync_tokens, count);
}
}  // extern "C"
