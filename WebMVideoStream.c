// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include <QuickTime/QuickTime.h>
#include "WebMExportStructs.h"
#include "WebMVideoStream.h"

//callback function of the decompression session
static void _frameDecompressedCallback(void *refCon, OSStatus inerr,
                                       ICMDecompressionTrackingFlags decompressionFlags,
                                       CVPixelBufferRef pixelBuffer, TimeValue64 displayTime,
                                       TimeValue64 displayDuration,
                                       ICMValidTimeFlags validTimeFlags, void *reserved,
                                       void *sourceFrameRefCon)
{
    OSStatus err;
    dbg_printf("[webM]  >> _frameDecompressedCallback(err=%d)\n", inerr);

    if (!inerr)
    {
        VideoStreamPtr vs = (VideoStreamPtr) refCon;

        if (decompressionFlags & kICMDecompressionTracking_ReleaseSourceData)
        {
            // if we were responsible for managing source data buffers,
            //  we should release the source buffer here,
            //  using sourceFramemovieGet.refCon to identify it.
        }

        if ((decompressionFlags & kICMDecompressionTracking_EmittingFrame) && pixelBuffer)
        {
            ICMCompressionFrameOptionsRef frameOptions = NULL;
            OSType pf = CVPixelBufferGetPixelFormatType(pixelBuffer);

            dbg_printf("[webM]   _frame_decompressed() = %ld; dFlags %ld,"
                       " dispTime %lld, dispDur %lld, timeFlags %ld [%ld '%4.4s' (%ld x %ld)]\n",
                       inerr, decompressionFlags, displayTime, displayDuration, validTimeFlags,
                       CVPixelBufferGetDataSize(pixelBuffer), (char *) &pf,
                       CVPixelBufferGetWidth(pixelBuffer),
                       CVPixelBufferGetHeight(pixelBuffer));

            displayDuration = 25; // TODO I'm not sure why 25 was good

            // Feed the frame to the compression session.
            dbg_printf("feeding the frame to the compression session\n");
            err = ICMCompressionSessionEncodeFrame(vs->compressionSession, pixelBuffer,
                                                   displayTime, displayDuration,
                                                   validTimeFlags, frameOptions,
                                                   NULL, NULL);
        }
    }

    dbg_printf("[webM] exit _frameDecompressedCallback err= %ld\n", err);

}

static void addint32toDictionary(CFMutableDictionaryRef dictionary, CFStringRef key, SInt32 i)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &i);

    if (!number) return;

    CFDictionaryAddValue(dictionary, key, number);
    CFRelease(number);
}
static void addShortToDictionary(CFMutableDictionaryRef dictionary, CFStringRef key, short i)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberShortType, &i);

    if (!number) return;

    CFDictionaryAddValue(dictionary, key, number);
    CFRelease(number);
}


ComponentResult openDecompressionSession(VideoStreamPtr vs)
{
    ImageDescriptionHandle idh = (ImageDescriptionHandle)(vs->source.params.desc);

    ComponentResult err = noErr;
    CFMutableDictionaryRef pixelBufferAttributes = NULL;
    ICMDecompressionTrackingCallbackRecord trackingCallBackRecord;
    OSType pixelFormat = k422YpCbCr8PixelFormat;
    ImageDescription *id = *idh;

    //create a dictionary describingg the pixel buffer we want to get back
    pixelBufferAttributes = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    addShortToDictionary(pixelBufferAttributes, kCVPixelBufferWidthKey, id->width);
    addShortToDictionary(pixelBufferAttributes, kCVPixelBufferHeightKey, id->height);
    addint32toDictionary(pixelBufferAttributes, kCVPixelBufferHeightKey, pixelFormat);


    //call back function for the decompression session
    trackingCallBackRecord.decompressionTrackingCallback = _frameDecompressedCallback;
    trackingCallBackRecord.decompressionTrackingRefCon = (void *) vs; //reference to pass to the callback function

    err = ICMDecompressionSessionCreate(NULL, idh, /* sessionOptions */ NULL, pixelBufferAttributes,
                                        &trackingCallBackRecord, &vs->decompressionSession);
    return err;
}


static OSStatus
_frame_compressed_callback(void *efRefCon, ICMCompressionSessionRef session,
                           OSStatus err, ICMEncodedFrameRef ef, void *reserved)
{
    dbg_printf("[webM]  enter _frame_compressed_callback(err = %d)\n", err);
    VideoStreamPtr vs = (VideoStreamPtr) efRefCon;

    if (err)
        return err;

    UInt32 enc_size = ICMEncodedFrameGetDataSize(ef);
    ImageDescriptionHandle imgDesc;

    err = ICMEncodedFrameGetImageDescription(ef, &imgDesc);

    if (!err)
    {
        ImageDescription *id = *imgDesc;

        dbg_printf("[webM --%08lx] :: _frame_compressed() = %ld, '%4.4s'"
                   " %08lx %08lx [%d x %d] [%f x %f] %ld %d %d %d\n",
                   (UInt32) - 1, err, (char *) &id->cType,
                   id->temporalQuality, id->spatialQuality, id->width,
                   id->height, id->hRes / 65536.0, id->vRes / 65536.0,
                   id->dataSize, id->frameCount, id->depth, id->clutID);
        dbg_printf("[webM -- %08lx] :: _frame_compressed() = %lld %ld %ld\n",
                   (UInt32) - 1, ICMEncodedFrameGetDecodeDuration(ef),
                   enc_size, ICMEncodedFrameGetBufferSize(ef));
    }

    vs->frame_type = ICMEncodedFrameGetFrameType(ef);

    dbg_printf("[webM] allocate data %d bytes (current buf = %d)\n", enc_size, vs->outBuf.data);

    if (vs->outBuf.size != enc_size)
    {
        allocBuffer(&vs->outBuf, enc_size);
    }

    memcpy(vs->outBuf.data, ICMEncodedFrameGetDataPtr(ef), enc_size);
    dbg_printf("[webM]  exit _frame_compressed_callback(err = %d)\n", err);
    return err;
}

//Using the componentInstance, load settings and then pass them to the compression session
//this in turn sends all these parameters to the VP8 Component
static ComponentResult setCompressionSettings(WebMExportGlobalsPtr glob, ICMCompressionSessionOptionsRef options)
{
    ComponentInstance videoCI = NULL;
    
    ComponentResult err = getVideoComponentInstace(glob, &videoCI);
    if(err) goto bail;
    
//  --- Hardcoded settings ---
    //Allow P Frames
    err = ICMCompressionSessionOptionsSetAllowTemporalCompression(options, true);
    if (err) goto bail;
    
    // Disable B frames.
    err = ICMCompressionSessionOptionsSetAllowFrameReordering(options, false);
    
    
//  ---  Transfer spatial settings   ----
    SCSpatialSettings ss;
    err = SCGetInfo(videoCI, scSpatialSettingsType, &ss);
    if (err) goto bail;

    dbg_printf("[WebM]Spatial settings - depth %d Quality %lx\n", ss.depth, ss.spatialQuality);

//  ------ Transfer Temporal Settings   --------
    SCTemporalSettings ts;
    err = SCGetInfo(videoCI, scTemporalSettingsType, &ts);
    if (err) goto bail;
    dbg_printf("[WebM] Temporal Settings max keyframerate %ld quality %ld frameRate %f\n",
               ts.keyFrameRate, ts.temporalQuality,FixedToFloat(ts.frameRate));

    SInt32 keyFrameRate = ts.keyFrameRate;
    if (keyFrameRate != 0)
    {
        err = ICMCompressionSessionOptionsSetMaxKeyFrameInterval(options,
                keyFrameRate);
        if (err) goto bail;
    }
    err = ICMCompressionSessionOptionsSetProperty(options,
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_Quality,
                                                  sizeof(CodecQ), &ts.temporalQuality);
    if (err) goto bail;
    
    err = ICMCompressionSessionOptionsSetProperty(options,
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_ExpectedFrameRate,
                                                  sizeof(Fixed), &ts.frameRate);
    if (err) goto bail;
    
    
//  ------  Transfer Datarate Settings   ----
    SCDataRateSettings ds;  
    err = SCGetInfo(videoCI, scDataRateSettingsType, &ds);
    dbg_printf("[webm] DataRateSettings %ld frameDuration %ld, spatial Quality %d, temporal Quality %d\n", 
               ds.dataRate, ds.frameDuration, ds.minSpatialQuality, ds.minTemporalQuality);
    if (err) goto bail;
    if (ds.dataRate != 0)
    {
        err = ICMCompressionSessionOptionsSetProperty(options,
                                                kQTPropertyClass_ICMCompressionSessionOptions,
                                                kICMCompressionSessionOptionsPropertyID_AverageDataRate,
                                                sizeof(SInt32), &ds.dataRate); 
        if (err) goto bail;
    }
    
    
    
    QTUnlockContainer(glob->videoSettingsAtom);

bail:

    if (videoCI != NULL)
        CloseComponent(videoCI);
    if (err)
        dbg_printf("[webm] Error in setCompressionSettingFromAC %d\n", err);
    
    return err;
}


ComponentResult openCompressionSession(WebMExportGlobalsPtr globals, VideoStreamPtr vs)
{
    ComponentResult err = noErr;
    ICMCompressionSessionOptionsRef options;
    ICMEncodedFrameOutputRecord efor;

    ImageDescription *id = *(ImageDescriptionHandle) vs->source.params.desc;
    int width = id->width;
    int height = id->height;

    StreamSource *source = &vs->source;

    err = ICMCompressionSessionOptionsCreate(NULL, &options);

    if (err) goto bail;

    ICMCompressionSessionOptionsSetDurationsNeeded(options, true);


    setCompressionSettings(globals, options);

    efor.encodedFrameOutputCallback = _frame_compressed_callback;
    efor.encodedFrameOutputRefCon = (void *) vs;
    efor.frameDataAllocator = NULL;

    dbg_printf("[webM] openCompressionSession timeScale = %ld (%d x %d)\n",
               vs->source.timeScale, width, height);
    err = ICMCompressionSessionCreate(NULL, width,
                                      height,
                                      'VP80', /* fixed for now... */
                                      vs->source.timeScale, options,
                                      NULL, &efor, &vs->compressionSession);
    dbg_printf("[webM] created compression Session %d\n", err);

bail:

    if (err)
        dbg_printf("[WebM] Open Compression Session error = \n", err);

    return err;
}


static void initFrameTimeRecord(VideoStreamPtr vs, ICMFrameTimeRecord *frameTimeRecord)
{
    dbg_printf("[webM] init Frame #%ld scale = %ld", vs->currentFrame, vs->source.timeScale);
    memset(frameTimeRecord, 0, sizeof(ICMFrameTimeRecord));
    frameTimeRecord->recordSize = sizeof(ICMFrameTimeRecord);
    //si->source.time is in milliseconds whereas this value needs to be in timeScale Units
    //*(TimeValue64 *) &frameTimeRecord->value = vs->currentFrame * vs->source.params.durationPerSample / si->source.timeScale;
    *(TimeValue64 *) &frameTimeRecord->value = vs->source.time;
    frameTimeRecord->scale = vs->source.timeScale;
    frameTimeRecord->rate = fixed1;
    frameTimeRecord->duration = vs->source.params.durationPerSample;
    frameTimeRecord->flags = icmFrameTimeDecodeImmediately;
    frameTimeRecord->frameNumber = vs->currentFrame;
    dbg_printf("[webM] init Frame Time %lld scale = %ld", frameTimeRecord->value, frameTimeRecord->scale);
}

//buffer is null when there are no more frames
ComponentResult compressNextFrame(WebMExportGlobalsPtr globals, VideoStreamPtr vs)
{
    ComponentResult err = noErr;
    ICMFrameTimeRecord frameTimeRecord;

    initMovieGetParams(&vs->source);
    err = InvokeMovieExportGetDataUPP(vs->source.refCon, &vs->source.params,
                                      vs->source.dataProc);

    if (err == eofErr)
    {
        dbg_printf("[WebM] Video Stream eos eofErr\n");
        vs->source.eos = true;
        return noErr;
    }
    else if (err != noErr)
        return err;

    if (vs->source.params.actualSampleCount == 0 && vs->currentFrame != 0)
    {
        //dbg_printf("[WebM] no samples Read (eos causing issue)\n");
        //vs->source.eos = true;
        //return noErr;
    }

    dbg_printDataParams(&vs->source);
    initFrameTimeRecord(vs, &frameTimeRecord);

    if (vs->decompressionSession == NULL)
    {
        err = openDecompressionSession(vs);
        dbg_printf("[webM] open new decompression session %d\n", err);

        if (err != noErr) return err;
    }

    if (vs->compressionSession == NULL)
    {
        err = openCompressionSession(globals, vs);
        dbg_printf("[webM] open new compression session %d\n", err);

        if (err != noErr) return err;
    }

    //the callback function also compresses, the compressed data is then kept in vs->outBuf.data
    dbg_printf("[webm] Call ICMDecompressionSessionDecodeFrame\n");
    err = ICMDecompressionSessionDecodeFrame(vs->decompressionSession,
            (UInt8 *) vs->source.params.dataPtr,
            vs->source.params.dataSize,
            /* sess_opts */ NULL,
            &frameTimeRecord, vs);
    dbg_printf("exit compressFrame err = %d\n", err);

    return err;
}

ComponentResult initVideoStream(VideoStreamPtr vs)
{
    memset(vs, 0, sizeof(VideoStream));
    initBuffer(&vs->outBuf);

}
