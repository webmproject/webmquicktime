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
    UInt32 offset;
}WebMBuffer;

enum{
    KEY_FRAME = 0x01,
    VIDEO_FRAME = 0x02,
    AUDIO_FRAME = 0x04,
    ALT_REF_FRAME = 0x08
};

typedef struct
{
    void *data;
    UInt32 size;
    UInt32 offset; //pointer to the end of output buffers data.
    UInt64 timeMs; //time in milliseconds
    UInt32 frameType;  //corresponds to above frame types
    UInt32 indx;
} WebMBufferedFrame;


//these frames should be queued chronologically
typedef struct
{
    WebMBufferedFrame** queue;
    int queueSize;  //the maximum allocated memory
    int size;
} WebMQueuedFrames;


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
    UInt32 frameInIndx;  //last frame sent in
} StreamSource;



void initFrameQueue(WebMQueuedFrames *queue);
WebMBufferedFrame* getFrame(WebMQueuedFrames *queue);
void releaseFrame(WebMQueuedFrames *queue);
// returns -1 on memory error
int addFrameToQueue(WebMQueuedFrames *queue, void * data,UInt32 size, UInt64 timeMs, UInt32 frameType, UInt32 indx);
int frameQueueSize(WebMQueuedFrames *queue);
int freeFrameQueue(WebMQueuedFrames *queue);

void initMovieGetParams(StreamSource *get);
void dbg_printDataParams(StreamSource *get);
ComponentResult initStreamSource(StreamSource *source,  TimeScale scale,
                                 long trackID, MovieExportGetPropertyUPP propertyProc,
                                 MovieExportGetDataUPP getDataProc, void *refCon);
double getTimeAsSeconds(StreamSource *source);

#endif