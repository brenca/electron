// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef NATIVE_API_ELECTRON_H_
#define NATIVE_API_ELECTRON_H_

#ifdef _WIN32
#ifndef BUILDING_NODE_EXTENSION
#define ELECTRON_EXTERN __declspec(dllexport)
#else
#define ELECTRON_EXTERN __declspec(dllimport)
#endif
#else
#define ELECTRON_EXTERN __attribute__((visibility("default")))
#endif

#endif  // NATIVE_API_ELECTRON_H_
