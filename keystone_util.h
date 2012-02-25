// Copyright (c) 2012 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#if !defined WEBMQUICKTIME_KEYSTONE_UTIL_H_
#define WEBMQUICKTIME_KEYSTONE_UTIL_H_

#if defined __cplusplus
extern "C" {
#endif

// Touches the file |kWebmBundleId| in
// $HOME/Library/Google/GoogleSoftwareUpdate/Actives. Creates the directories
// in the path when necessary.
void TouchActivityFile();

#if defined __cplusplus
}  // extern "C"
#endif

#endif  // WEBMQUICKTIME_KEYSTONE_UTIL_H_
