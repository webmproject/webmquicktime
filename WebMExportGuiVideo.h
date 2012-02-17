// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#ifndef _WEBMEXPORTGUIVIDEO_H_
#define _WEBMEXPORTGUIVIDEO_H_ 1

#if !defined(kWebMExportBundleID)
#define kWebMExportBundleID "org.webmproject.webmquicktime.component"
#endif

ComponentResult getDefaultVP8Atom(WebMExportGlobalsPtr globals);
ComponentResult OpenVP8Dlg(WebMExportGlobalsPtr globals, WindowRef window);
ComponentResult getVideoComponentInstace(WebMExportGlobalsPtr glob, ComponentInstance *videoCI);


#endif
