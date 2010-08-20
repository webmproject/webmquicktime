// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#ifndef _MKV_EXPORT_GUI_H_
#define _MKV_EXPORT_GUI_H_


#include "WebMExportGuiVideo.h"
#include "WebMExportGuiAudio.h"

pascal OSStatus SettingsWindowEventHandler(EventHandlerCallRef inHandler, EventRef inEvent, void *inUserData);
ComponentResult checkMovieHasVideoAudio(WebMExportGlobalsPtr globals, Movie theMovie, Track onlyThisTrack);
ComponentResult getWindow(WindowRef *window);

#endif