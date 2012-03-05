// Copyright (c) 2012 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#if !defined WEBMQUICKTIME_QUICKTIME_UTIL_H_
#define WEBMQUICKTIME_QUICKTIME_UTIL_H_

#if defined __cplusplus
extern "C" {
#endif

// Returns true when the XiphQT Vorbis component is accessible through the
// QuickTime component management interface.
bool CanExportVorbisAudio();

#if defined __cplusplus
}  // extern "C"
#endif

#endif  // WEBMQUICKTIME_QUICKTIME_UTIL_H_
