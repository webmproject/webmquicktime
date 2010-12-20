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

OSStatus EnableMultiPassWithTemporaryFile(ICMCompressionSessionOptionsRef inCompressionSessionOptions,
                                          ICMMultiPassStorageRef *outMultiPassStorage)
{
  FSRef tempDirRef;
  ICMMultiPassStorageRef multiPassStorage = NULL;

  OSStatus status;

  *outMultiPassStorage = NULL;

  // users temp directory
  status = FSFindFolder(kUserDomain, kTemporaryFolderType,
                        kCreateFolder, &tempDirRef);
  if (noErr != status) goto bail;

  // create storage using a temporary file with a unique file name
  status = ICMMultiPassStorageCreateWithTemporaryFile(kCFAllocatorDefault,
                                                      &tempDirRef,
                                                      NULL, 0,
                                                      &multiPassStorage);
  if (noErr != status) goto bail;

  // enable multi-pass by setting the compression session options
  // note - the compression session options object retains the multi-pass
  // storage object
  status = ICMCompressionSessionOptionsSetProperty(inCompressionSessionOptions,
                                                   kQTPropertyClass_ICMCompressionSessionOptions,
                                                   kICMCompressionSessionOptionsPropertyID_MultiPassStorage,
                                                   sizeof(ICMMultiPassStorageRef),
                                                   &multiPassStorage);

bail:
  if (noErr != status) {
    // this api is NULL safe so we can just call it
    ICMMultiPassStorageRelease(multiPassStorage);
  } else {
    *outMultiPassStorage = multiPassStorage;
  }

  return status;
}




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
    GenericStreamPtr vs = (GenericStreamPtr) refCon;


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

      // Feed the frame to the compression session.
      dbg_printf("feeding the frame to the compression session\n");
      err = ICMCompressionSessionEncodeFrame(vs->vid.compressionSession, pixelBuffer,
                                             displayTime, displayDuration,
                                             validTimeFlags, frameOptions,
                                             NULL, NULL);
      if (err !=0)
      {
        const char* errString = GetMacOSStatusErrorString(err);
        dbg_printf("[WebM] ICMCompressionSessionEncodeFrame err = %s\n", errString);
      }
    }
    if (decompressionFlags & kICMDecompressionTracking_ReleaseSourceData)
    {
      // if we were responsible for managing source data buffers,
      //  we should release the source buffer here,
      //  using sourceFramemovieGet.refCon to identify it.

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


ComponentResult openDecompressionSession(GenericStreamPtr vs)
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
                                      &trackingCallBackRecord, &vs->vid.decompressionSession);
  return err;
}


static OSStatus
_frame_compressed_callback(void *efRefCon, ICMCompressionSessionRef session,
                           OSStatus err, ICMEncodedFrameRef ef, void *reserved)
{
  dbg_printf("[webM]  enter _frame_compressed_callback(err = %d)\n", err);
  GenericStreamPtr vs = (GenericStreamPtr) efRefCon;

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


  dbg_printf("[webM] allocate data %d bytes\n", enc_size);


  //pass appropriate flags for frame type
  ICMFrameType frame_type = ICMEncodedFrameGetFrameType(ef);
  UInt16 frameFlags = VIDEO_FRAME;

  //NOTE: With Altref frames, the decodeTimeStamp Represents the altref time
  //  this is a workaround that I don't like, ideally some other sort of
  //  parameter could handle this.

  UInt64 displayTime = ICMEncodedFrameGetDisplayTimeStamp(ef);
  UInt64 timeScale = ICMEncodedFrameGetTimeScale(ef);

  dbg_printf("[WebM] ICMEncodedFrameGetDisplayTimeStamp %llu ICMEncodedFrameGetTimeScale %llu\n",
             displayTime, timeScale);

  UInt32 decodeNum = ICMEncodedFrameGetDecodeNumber(ef);
  UInt32 timeMs = displayTime * 1000/timeScale;
  dbg_printf("[WebM] adding frame %d to queue %lu flags %lx\n", decodeNum, timeMs, frameFlags);

  if (frame_type != kICMFrameType_Unknown)
  {
    if (frame_type == kICMFrameType_I)
      frameFlags += KEY_FRAME;
    //create a buffer with frame data
    void * buf = malloc(enc_size);
    memcpy(buf, ICMEncodedFrameGetDataPtr(ef), enc_size);

    addFrameToQueue(&vs->frameQueue, buf, enc_size,  timeMs, frameFlags, decodeNum -1);
  }
  else
  {
    //There are two frames embedded in altref frames...
    UInt32 decodeTimeMs = vs->vid.lastTimeMs + (timeMs - vs->vid.lastTimeMs)/2;
    const unsigned char * cBuf = ICMEncodedFrameGetDataPtr(ef);
    UInt32 altrefPortion= *((UInt32*)cBuf);
    dbg_printf("[WebM]Size Of altref data in frame %lu at time %lu\n", altrefPortion, decodeTimeMs);
    void * altrefBuf = malloc(altrefPortion);
    memcpy(altrefBuf, &cBuf[4], altrefPortion);
    frameFlags += ALT_REF_FRAME; // currently using the unknown to indicat alt-ref
    addFrameToQueue(&vs->frameQueue, altrefBuf, altrefPortion,  decodeTimeMs, frameFlags, decodeNum -1);

    //also write the following interframe
    UInt32 framePortion = enc_size - altrefPortion -4;
    dbg_printf("[WebM]Size Of inter frame %lu at time %lu\n", framePortion, timeMs);
    frameFlags = VIDEO_FRAME;
    void * frameBuf = malloc(altrefPortion);
    memcpy(frameBuf, &cBuf[altrefPortion+4], altrefPortion);
    addFrameToQueue(&vs->frameQueue, frameBuf, framePortion,  timeMs, frameFlags, decodeNum -1);
  }

  vs->vid.lastTimeMs = timeMs;

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
  if (err) goto bail;

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

  //  ------  Transfer Custom Settings   ----
  if (glob->videoSettingsCustom == NULL)
  {
    glob->videoSettingsCustom = NewHandleClear(0);
    SetHandleSize(glob->videoSettingsCustom, 0);
  }
  err = SCGetInfo(videoCI, scCodecSettingsType, &glob->videoSettingsCustom);
  if (glob->videoSettingsCustom != NULL)
  {
    err = ICMCompressionSessionOptionsSetProperty(options,
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_CompressorSettings,
                                                  sizeof(glob->videoSettingsCustom),
                                                  &glob->videoSettingsCustom);
  }

  QTUnlockContainer(glob->videoSettingsAtom);

bail:

  if (videoCI != NULL)
    CloseComponent(videoCI);
  if (err)
    dbg_printf("[webm] Error in setCompressionSettingFromAC %d\n", err);

  return err;
}


ComponentResult openCompressionSession(WebMExportGlobalsPtr globals, GenericStreamPtr vs)
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

  //Even though VP8 doesn't use this, I am enabling a temporary storage file
  //  This is so quick time allows the two pass
  if (vs->vid.bTwoPass)
  {
    ICMMultiPassStorageRef multiPassStorage = NULL;
    OSStatus status = EnableMultiPassWithTemporaryFile(options, &multiPassStorage);
    if (noErr != status) goto bail;
    ICMMultiPassStorageRelease(multiPassStorage);
  }

  dbg_printf("[webM] openCompressionSession timeScale = %ld (%d x %d)\n",
             vs->source.timeScale, width, height);
  err = ICMCompressionSessionCreate(NULL, width,
                                    height,
                                    'VP80', /* fixed for now... */
                                    vs->source.timeScale, options,
                                    NULL, &efor, &vs->vid.compressionSession);
  dbg_printf("[webM] created compression Session %d\n", err);
  if(err) goto bail;

  //After initializing the compression session, set passes if there are two passes
  if (vs->vid.bTwoPass)
    err = startPass(vs,1);

bail:

  if (err)
    dbg_printf("[WebM] Open Comprlession Session error = \n", err);

  return err;
}


static void initFrameTimeRecord(GenericStreamPtr vs, ICMFrameTimeRecord *frameTimeRecord)
{
  memset(frameTimeRecord, 0, sizeof(ICMFrameTimeRecord));
  frameTimeRecord->recordSize = sizeof(ICMFrameTimeRecord);
  //si->source.time is in milliseconds whereas this value needs to be in timeScale Units
  //*(TimeValue64 *) &frameTimeRecord->value = vs->currentFrame * vs->source.params.durationPerSample / si->source.timeScale;
  frameTimeRecord->value = SInt64ToWide(vs->source.time);
  frameTimeRecord->scale = vs->source.timeScale;
  frameTimeRecord->rate = fixed1;
  frameTimeRecord->duration = vs->source.params.durationPerSample;
  frameTimeRecord->flags = icmFrameTimeDecodeImmediately;
  frameTimeRecord->frameNumber = vs->framesIn;
  dbg_printf("[webM] init Frame Time %lld scale = %ld\n", frameTimeRecord->value, frameTimeRecord->scale);
}

//buffer is null when there are no more frames
ComponentResult compressNextFrame(WebMExportGlobalsPtr globals, GenericStreamPtr vs)
{
  ComponentResult err = noErr;
  ICMFrameTimeRecord frameTimeRecord;
  initMovieGetParams(&vs->source);
  err = InvokeMovieExportGetDataUPP(vs->source.refCon, &vs->source.params,
                                    vs->source.dataProc);

  if (err == eofErr)
  {
    vs->source.eos = true;
    err= noErr;
  }
  else
  {
    vs->framesIn += 1;
  }

  if (err != noErr)
    return err;

  dbg_printDataParams(&vs->source);
  initFrameTimeRecord(vs, &frameTimeRecord);

  if (vs->vid.decompressionSession == NULL)
  {
    err = openDecompressionSession(vs);
    dbg_printf("[webM] open new decompression session %d\n", err);

    if (err != noErr) return err;
  }

  if (vs->vid.compressionSession == NULL)
  {
    err = openCompressionSession(globals, vs);
    dbg_printf("[webM] open new compression session %d\n", err);

    if (err != noErr) return err;
  }

  //the callback function also compresses, the compressed data is then kept in vs->outBuf.data
  dbg_printf("[webm] Call ICMDecompressionSessionDecodeFrame\n");
  if (!vs->source.eos)
  {
    dbg_printf("[WebM] Setting frame time to %lld\n",frameTimeRecord.value, frameTimeRecord.scale);
    err = ICMDecompressionSessionDecodeFrame(vs->vid.decompressionSession,
                                             (UInt8 *) vs->source.params.dataPtr,
                                             vs->source.params.dataSize,
                                             NULL,  //session options
                                             &frameTimeRecord, vs);
  }
  else
  {
    dbg_printf("Completing Frames\n");
    ICMCompressionSessionCompleteFrames(vs->vid.compressionSession,
                                        true,  //complete all frames
                                        0, //ignored when complete all frames true
                                        0);  //also ignored
    vs->complete = true;
  }
  //increment next source time
  double framerate = globals->framerate;
  if (framerate == 0)
  {
    globals->framerate = (1.0 * vs->source.params.sourceTimeScale) / (1.0 * vs->source.params.durationPerSample );
    dbg_printf("[webm] Recomputing framerate %ld / %ld\n",
               vs->source.params.durationPerSample, vs->source.params.sourceTimeScale);
    framerate = globals->framerate;
  }
  vs->source.time = (SInt32)(vs->framesIn  / globals->framerate * vs->source.timeScale) ;
  dbg_printf("exit compressFrame err = %d\n", err);
  return err;
}

ComponentResult initVideoStream(GenericStreamPtr vs)
{
  memset(vs, 0, sizeof(GenericStreamPtr));
}

ComponentResult startPass(GenericStreamPtr vs,int pass)
{
  ComponentResult err = noErr;
  ICMCompressionPassModeFlags cpFlag =0;
  dbg_printf("[WEBM] Start pass %d session %ld\n", pass, (UInt32) vs->vid.compressionSession);
  if (pass == 1)
  {
    ICMCompressionPassModeFlags readFlags;
    Boolean bSupports = ICMCompressionSessionSupportsMultiPassEncoding(vs->vid.compressionSession,
                                                                       0, &readFlags);
    dbg_printf("[WebM] Supports multipass %d, flags %u\n", bSupports, readFlags);
    cpFlag = kICMCompressionPassMode_WriteToMultiPassStorage;
  }
  else if (pass == 2)
    cpFlag = kICMCompressionPassMode_OutputEncodedFrames | kICMCompressionPassMode_ReadFromMultiPassStorage;
  else
    return paramErr;

  OSStatus os= ICMCompressionSessionBeginPass(vs->vid.compressionSession, cpFlag, 0);
  if (os)
  {
    dbg_printf("[WebM] Error on ICMCompressionSessionBeginPass %d\n", os);
    err = os;  //TODO choose an apropriate error message
  }
  return err;
}

OSStatus endPass(GenericStreamPtr vs)
{
  dbg_printf("[WEBM] End pass session %ld\n", (UInt32) vs->vid.compressionSession);
  OSStatus os = ICMCompressionSessionEndPass(vs->vid.compressionSession);
  if (os != 0)
    dbg_printf("[WEBM] Error: endPass got error %d\n", os);

  return os;
}
