// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#ifndef _WEBM_COMMON_H_
#define _WEBM_COMMON_H_ 1
//Here are some common data structs and functions to support them
#if defined(__APPLE_CC__)
#include <QuickTime/QuickTime.h>
#else
#include <QuickTimeComponents.h>

#if defined(TARGET_OS_WIN32)
#define _WINIOCTL_
#error windows
#include <windows.h>
#endif /* TARGET_OS_WIN32 */

#endif /* __APPLE_CC__ */


typedef struct
{
    void *data;
    UInt32 size;
    UInt32 offset; //pointer to the end of output buffers data.
} WebMBuffer;

typedef struct
{
    MovieExportGetPropertyUPP propertyProc;
    MovieExportGetDataUPP dataProc;
    void *refCon;
    MovieExportGetDataParams params;
    Boolean eos;
    SInt32 time;                    //This time must be based on the source Time scale
    TimeScale timeScale;
    long trackID;
    Boolean bQdFrame;
    unsigned long blockTimeMs;
} StreamSource;


int freeBuffer(WebMBuffer *buf);
int allocBuffer(WebMBuffer *buf, size_t size);
void initBuffer(WebMBuffer *buf);
void initMovieGetParams(StreamSource *get);
void dbg_printDataParams(StreamSource *get);
ComponentResult initStreamSource(StreamSource *source,  TimeScale scale,
                                 long trackID, MovieExportGetPropertyUPP propertyProc,
                                 MovieExportGetDataUPP getDataProc, void *refCon);
double getTimeAsSeconds(StreamSource *source);

#endif