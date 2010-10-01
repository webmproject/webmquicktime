// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



#ifndef _WEBM_EXPORT_STRUCTS_h_
#define _WEBM_EXPORT_STRUCTS_h_ 1

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
#include "EbmlDataHWriter.h"
#include "WebMCommon.h"


typedef struct
{
    StreamSource source;
    WebMBuffer         outBuf;

    ComponentInstance vorbisComponentInstance;
    AudioStreamBasicDescription asbd;
    UInt64 currentEncodedFrames;
    UInt64 framesIn;
} AudioStream, *AudioStreamPtr;



typedef struct
{
    StreamSource source;
    WebMBuffer         outBuf;

    ICMDecompressionSessionRef decompressionSession;
    ICMCompressionSessionRef compressionSession;

    ICMFrameType        frame_type;
    unsigned int        currentFrame;   //the current frame being encoded.
    Boolean             bTwoPass;
} VideoStream, *VideoStreamPtr;

typedef struct
{
    OSType trackType;
    union
    {
        VideoStream vid;
        AudioStream aud;
    } ;
} GenericStream;

typedef struct
{
    UInt64 loc;
    unsigned long timeVal;
    unsigned int  track;
    unsigned int blockNumber;
} WebMCuePoint;

typedef struct
{
    ComponentInstance  self;
    ComponentInstance  quickTimeMovieExporter;

    int             streamCount;
    GenericStream    **streams;  //should be either audio or video

    unsigned long   cueCount;
    Handle          cueHandle;

    MovieProgressUPP   progressProc;
    long               progressRefCon;
    Boolean            progressOpen;

    Boolean            canceled;

    double              framerate;
    UInt32             webmTimeCodeScale;

    /* settings */
    Boolean             bExportVideo;
    Boolean             bExportAudio;

    AudioStreamBasicDescription audioBSD;

    //Ebml writing
    unsigned long clusterTime;
    EbmlLoc clusterStart;

    /////////////////
    QTAtomContainer     audioSettingsAtom;      //hold on to any audio settings the user changes
    QTAtomContainer     videoSettingsAtom;
    Handle              videoSettingsCustom;     //this contains vp8 custom settings.
    unsigned int        currentPass;
    
    /* settings dialog vars */
    Boolean            bMovieHasAudio;
    Boolean            bMovieHasVideo;
    Movie              setdlg_movie;
    Track              setdlg_track;
} WebMExportGlobals, *WebMExportGlobalsPtr;

#endif /* __exporter_types_h__ */
