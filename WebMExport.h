// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#ifndef __MKV_EXPORT_H__
#define __MKV_EXPORT_H__ 1

//#include "config.h"
#include "WebMExportVersions.h"



//#define kExporterResID                  4040
//#define kExporterNameStringResID        4040
//#define kExporterInfoStringResID        4041

#define kSoundComponentManufacturer     'goog'
#define kCodecFormat                    'VP80'



#ifdef _DEBUG
#define WebMExporterName        "MKV Exporter"
#else
#define WebMExporterName        ""  //TODO this seems wrong
#endif

#endif
