
// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#define HAVE_CONFIG_H "vpx_codecs_config.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vpx_codec_impl_top.h"
#include "vpx/vpx_codec_impl_bottom.h"
#include "vpx/vp8cx.h"

#if __APPLE_CC__
#include <QuickTime/QuickTime.h>
#else
#include <ConditionalMacros.h>
#include <Endian.h>
#include <ImageCodec.h>
#endif

#include "log.h"
#include "Raw_debug.h"


#include "VP8CodecVersion.h"
#include "VP8Encoder.h"
#include "VP8EncoderEncode.h"
#include "VP8EncoderGui.h"


// Setup required for ComponentDispatchHelper.c
#define IMAGECODEC_BASENAME()       VP8_Encoder_
#define IMAGECODEC_GLOBALS()        VP8EncoderGlobals storage

#define CALLCOMPONENT_BASENAME()    IMAGECODEC_BASENAME()
#define CALLCOMPONENT_GLOBALS()     IMAGECODEC_GLOBALS()

#define QT_BASENAME()               CALLCOMPONENT_BASENAME()
#define QT_GLOBALS()                CALLCOMPONENT_GLOBALS()

#define COMPONENT_UPP_PREFIX()      uppImageCodec
#define COMPONENT_DISPATCH_FILE     "VP8EncoderDispatch.h"
#define COMPONENT_SELECT_PREFIX()   kImageCodec

#if __APPLE_CC__
#include <CoreServices/Components.k.h>
#include <QuickTime/ImageCodec.k.h>
#include <QuickTime/ImageCompression.k.h>
#include <QuickTime/ComponentDispatchHelper.c>
#else
#include <Components.k.h>
#include <ImageCodec.k.h>
#include <ImageCompression.k.h>
#include <ComponentDispatchHelper.c>
#endif

// Open a new instance of the component.
// Allocate component instance storage ("globals") and associate it with the new instance so that other
// calls will receive it.
// Note that "one-shot" component calls like CallComponentVersion and ImageCodecGetCodecInfo work by opening
// an instance, making that call and then closing the instance, so you should avoid performing very expensive
// initialization operations in a component's Open function.
ComponentResult
VP8_Encoder_Open(
                 VP8EncoderGlobals glob,
                 ComponentInstance self)
{
  ComponentResult err = noErr;
  dbg_printf("[vp8e - %08lx] Open Called\n", (UInt32)glob);

  glob = calloc(sizeof(VP8EncoderGlobalsRecord), 1);

  if (! glob)
  {
    err = memFullErr;
    goto bail;
  }

  SetComponentInstanceStorage(self, (Handle)glob);

  glob->self = self;
  glob->target = self;

  glob->nextDecodeNumber = 1;
  glob->frameCount = 0;
  glob->raw = NULL;
  glob->codec = NULL;
  glob->stats.sz =0;
  glob->stats.buf = NULL;
  //default to one pass
  glob->currentPass = VPX_RC_ONE_PASS;
  glob->sourceQueue.size = 0;
  glob->sourceQueue.max = 0;
  glob->sourceQueue.queue = NULL;
  glob->sourceQueue.frames_in =0;
  glob->sourceQueue.frames_out =0;
  glob->altRefFrame.buf =0;
  glob->altRefFrame.size =0;

  int i;
  for (i=0;i<TOTAL_CUSTOM_VP8_SETTINGS; i++)
  {
    glob->settings[i]= UINT_MAX;
  }

bail:
  dbg_printf("[vp8e - %08lx] Open Called exit %d \n", (UInt32)glob, err);
  return err;
}

// Closes the instance of the component.
// Release all storage associated with this instance.
// Note that if a component's Open function fails with an error, its Close function will still be called.
ComponentResult
VP8_Encoder_Close(
                  VP8EncoderGlobals glob,
                  ComponentInstance self)
{
  dbg_printf("[vp8e - %08lx]  Close Called\n", (UInt32)glob);

  if (glob)
  {
    if (glob->stats.buf != NULL)
    {
      free(glob->stats.buf);
      glob->stats.buf =NULL;
      glob->stats.sz=0;
    }

    if (glob->codec) //see if i've initialized the vpx_codec
    {
      if (vpx_codec_destroy(glob->codec))
        dbg_printf("[vp8e - %08lx] Failed to destroy codec\n", (UInt32)glob);

      free(glob->codec);
    }

    ICMCompressionSessionOptionsRelease(glob->sessionOptions);
    glob->sessionOptions = NULL;

    if (glob->raw)
    {
      vpx_img_free(glob->raw);
      free(glob->raw);
    }

    if (glob->sourceQueue.queue != NULL)
      free(glob->sourceQueue.queue);

    free(glob);
  }

  return noErr;
}

// Return the version of the component.
// This does not need to correspond in any way to the human-readable version numbers found in
// Get Info windows, etc.
// The principal use of component version numbers is to choose between multiple installed versions
// of the same component: if the component manager sees two components with the same type, subtype
// and manufacturer codes and either has the componentDoAutoVersion registration flag set,
// it will deregister the one with the older version.  (If componentAutoVersionIncludeFlags is also
// set, it only does this when the component flags also match.)
// By convention, the high short of the component version is the interface version, which Apple
// bumps when there is a major change in the interface.
// We recommend bumping the low short of the component version every time you ship a release of a component.
// The version number in the 'thng' resource should be the same as the number returned by this function.
ComponentResult
VP8_Encoder_Version(VP8EncoderGlobals glob)
{
  dbg_printf("[vp8e - %08lx] returning version %d\n", (UInt32)glob, kVP8CompressorVersion);
  return kVP8CompressorVersion;
}

// Sets the target for a component instance.
// When a component wants to make a component call on itself, it should make that call on its target.
// This allows other components to delegate to the component.
// By default, a component's target is itself -- see the Open function.
ComponentResult
VP8_Encoder_Target(VP8EncoderGlobals glob, ComponentInstance target)
{
  dbg_printf("[vp8e - %08lx] VP8_Encoder_Target\n", (UInt32)glob);
  glob->target = target;
  return noErr;
}

// Your component receives the ImageCodecGetCodecInfo request whenever an application calls the Image Compression Manager's GetCodecInfo function.
// Your component should return a formatted compressor information structure defining its capabilities.
// Both compressors and decompressors may receive this request.
ComponentResult
VP8_Encoder_GetCodecInfo(VP8EncoderGlobals glob, CodecInfo *info)
{
  dbg_printf("[vp8e - %08lx] GetCodecInfo called\n", (UInt32)glob);
  OSErr err = noErr;

  if (info == NULL)
  {
    err = paramErr;
  }
  else
  {
    CodecInfo **tempCodecInfo;

    err = GetComponentResource((Component)glob->self, codecInfoResourceType, 255, (Handle *)&tempCodecInfo);

    if (err == noErr)
    {
      *info = **tempCodecInfo;
      DisposeHandle((Handle)tempCodecInfo);
    }
  }

  dbg_printf("[vp8e - %08lx] GetCodecInfo exit %d\n", (UInt32)glob, err);

  return err;
}

// Return the maximum size of compressed data for the image in bytes.
// Note that this function is only used when the ICM client is using a compression sequence
// (created with CompressSequenceBegin, not ICMCompressionSessionCreate).
// Nevertheless, it's important to implement it because such clients need to know how much
// memory to allocate for compressed frame buffers.
ComponentResult
VP8_Encoder_GetMaxCompressionSize(
                                  VP8EncoderGlobals glob,
                                  PixMapHandle        src,
                                  const Rect         *srcRect,
                                  short               depth,
                                  CodecQ              quality,
                                  long               *size)
{
  dbg_printf("[vp8e - %08lx] VP8_Encoder_GetMaxCompressionSize\n", (UInt32)glob);
  ComponentResult err = noErr;
  size_t maxBytes = 0;

  if (! size)
    return paramErr;

  //this is a very large guess... but they did ask for the max.
  maxBytes = (srcRect->right - srcRect->left) * (srcRect->bottom - srcRect->top) / 4;

  *size = maxBytes;

bail:
  return err;
}

// Utility to add an SInt32 to a CFMutableDictionary.
static void
addNumberToDictionary(CFMutableDictionaryRef dictionary, CFStringRef key, SInt32 numberSInt32)
{
  CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &numberSInt32);

  if (! number)
    return;

  CFDictionaryAddValue(dictionary, key, number);
  CFRelease(number);
}

// Utility to add a double to a CFMutableDictionary.
static void
addDoubleToDictionary(CFMutableDictionaryRef dictionary, CFStringRef key, double numberDouble)
{
  CFNumberRef number = CFNumberCreate(NULL, kCFNumberDoubleType, &numberDouble);

  if (! number)
    return;

  CFDictionaryAddValue(dictionary, key, number);
  CFRelease(number);
}

// Utility to round up to a multiple of 16.
static int
roundUpToMultipleOf16(int n)
{
  if (0 != (n & 15))
    n = (n + 15) & ~15;

  return n;
}

// Create a dictionary that describes the kinds of pixel buffers that we want to receive.
// The important keys to add are kCVPixelBufferPixelFormatTypeKey,
// kCVPixelBufferWidthKey and kCVPixelBufferHeightKey.
// Many compressors will also want to set kCVPixelBufferExtendedPixels,
// kCVPixelBufferBytesPerRowAlignmentKey, kCVImageBufferGammaLevelKey and kCVImageBufferYCbCrMatrixKey.
static OSStatus
createPixelBufferAttributesDictionary(SInt32 width, SInt32 height,
                                      const OSType *pixelFormatList, int pixelFormatCount,
                                      CFMutableDictionaryRef *pixelBufferAttributesOut)
{
  OSStatus err = memFullErr;
  int i;
  CFMutableDictionaryRef pixelBufferAttributes = NULL;
  CFNumberRef number = NULL;
  CFMutableArrayRef array = NULL;
  SInt32 widthRoundedUp, heightRoundedUp, extendRight, extendBottom;

  pixelBufferAttributes = CFDictionaryCreateMutable(
                                                    NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  if (! pixelBufferAttributes) goto bail;

  array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

  if (! array) goto bail;

  // Under kCVPixelBufferPixelFormatTypeKey, add the list of source pixel formats.
  // This can be a CFNumber or a CFArray of CFNumbers.
  for (i = 0; i < pixelFormatCount; i++)
  {
    number = CFNumberCreate(NULL, kCFNumberSInt32Type, &pixelFormatList[i]);

    if (! number) goto bail;

    CFArrayAppendValue(array, number);

    CFRelease(number);
    number = NULL;
  }

  CFDictionaryAddValue(pixelBufferAttributes, kCVPixelBufferPixelFormatTypeKey, array);
  CFRelease(array);
  array = NULL;

  // Add kCVPixelBufferWidthKey and kCVPixelBufferHeightKey to specify the dimensions
  // of the source pixel buffers.  Normally this is the same as the compression target dimensions.
  addNumberToDictionary(pixelBufferAttributes, kCVPixelBufferWidthKey, width);
  addNumberToDictionary(pixelBufferAttributes, kCVPixelBufferHeightKey, height);

  // If you want to require that extra scratch pixels be allocated on the edges of source pixel buffers,
  // add the kCVPixelBufferExtendedPixels{Left,Top,Right,Bottom}Keys to indicate how much.
  // Internally our encoded can only support multiples of 16x16 macroblocks;
  // we will round the compression dimensions up to a multiple of 16x16 and encode that size.
  // (Note that if your compressor needs to copy the pixels anyhow (eg, in order to convert to a different
  // format) you may get better performance if your copy routine does not require extended pixels.)
  widthRoundedUp = roundUpToMultipleOf16(width);
  heightRoundedUp = roundUpToMultipleOf16(height);
  extendRight = widthRoundedUp - width;
  extendBottom = heightRoundedUp - height;

  if (extendRight || extendBottom)
  {
    addNumberToDictionary(pixelBufferAttributes, kCVPixelBufferExtendedPixelsRightKey, extendRight);
    addNumberToDictionary(pixelBufferAttributes, kCVPixelBufferExtendedPixelsBottomKey, extendBottom);
  }

  // Altivec code is most efficient reading data aligned at addresses that are multiples of 16.
  // Pretending that we have some altivec code, we set kCVPixelBufferBytesPerRowAlignmentKey to
  // ensure that each row of pixels starts at a 16-byte-aligned address.
  addNumberToDictionary(pixelBufferAttributes, kCVPixelBufferBytesPerRowAlignmentKey, 16);

  // This codec accepts YCbCr input in the form of '2vuy' format pixel buffers.
  // We recommend explicitly defining the gamma level and YCbCr matrix that should be used.
  addDoubleToDictionary(pixelBufferAttributes, kCVImageBufferGammaLevelKey, 2.2);
  CFDictionaryAddValue(pixelBufferAttributes, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4);

  err = noErr;
  *pixelBufferAttributesOut = pixelBufferAttributes;
  pixelBufferAttributes = NULL;

bail:

  if (pixelBufferAttributes) CFRelease(pixelBufferAttributes);

  if (number) CFRelease(number);

  if (array) CFRelease(array);

  return err;
}

// Prepare to compress frames.
// Compressor should record session and sessionOptions for use in later calls.
// Compressor may modify imageDescription at this point.
// Compressor may create and return pixel buffer options.
ComponentResult
VP8_Encoder_PrepareToCompressFrames(
                                    VP8EncoderGlobals glob,
                                    ICMCompressorSessionRef session,
                                    ICMCompressionSessionOptionsRef sessionOptions,
                                    ImageDescriptionHandle imageDescription,
                                    void *reserved,
                                    CFDictionaryRef *compressorPixelBufferAttributesOut)
{
  dbg_printf("[vp8e] Prepare to Compress Frames\n", (UInt32)glob);
  ComponentResult err = noErr;
  CFMutableDictionaryRef compressorPixelBufferAttributes = NULL;
  //This format later needs to be converted
  OSType pixelFormatList[] = { k422YpCbCr8PixelFormat }; // also known as '2vuy'

  Fixed gammaLevel;
  int frameIndex;
  SInt32 widthRoundedUp, heightRoundedUp;

  // Record the compressor session for later calls to the ICM.
  // Note: this is NOT a CF type and should NOT be CFRetained or CFReleased.
  glob->session = session;

  // Retain the session options for later use.
  ICMCompressionSessionOptionsRelease(glob->sessionOptions);
  glob->sessionOptions = sessionOptions;
  ICMCompressionSessionOptionsRetain(glob->sessionOptions);

  // Modify imageDescription here if needed.
  // We'll set the image description gamma level to say "2.2".
  gammaLevel = kQTCCIR601VideoGammaLevel;
  err = ICMImageDescriptionSetProperty(imageDescription,
                                       kQTPropertyClass_ImageDescription,
                                       kICMImageDescriptionPropertyID_GammaLevel,
                                       sizeof(gammaLevel),
                                       &gammaLevel);

  if (err)
    goto bail;

  // Record the dimensions from the image description.
  glob->width = (*imageDescription)->width;
  glob->height = (*imageDescription)->height;
  dbg_printf("[vp8e - %08lx] Prepare to compress frame width %d height %d\n", (UInt32)glob, glob->width, glob->height);

  if (glob->width < 16 || glob->width % 2 || glob->height < 16 || glob->height % 2)
    dbg_printf("[vp8e - %08lx] Warning :: Invalid resolution: %ldx%ld", (UInt32)glob, glob->width, glob->height);

  if (glob->raw == NULL)
    glob->raw = calloc(1, sizeof(vpx_image_t));

  //Right now I'm only using YV12, this is great for webm, as I control the spit component
  if (!vpx_img_alloc(glob->raw, IMG_FMT_YV12, glob->width, glob->height, 1))
  {
    dbg_printf("[vp8e - %08lx] Error: Failed to allocate image %dx%d", (UInt32)glob, glob->width, glob->height);
    err = paramErr;
    goto bail;
  }

  glob->maxEncodedDataSize = glob->width * glob->height * 2;
  dbg_printf("[vp8e - %08lx] currently allocating %d bytes as my max encoded size\n", (UInt32)glob, glob->maxEncodedDataSize);

  // Create a pixel buffer attributes dictionary.
  err = createPixelBufferAttributesDictionary(glob->width, glob->height,
                                              pixelFormatList, sizeof(pixelFormatList) / sizeof(OSType),
                                              &compressorPixelBufferAttributes);

  if (err)
    goto bail;

  *compressorPixelBufferAttributesOut = compressorPixelBufferAttributes;
  compressorPixelBufferAttributes = NULL;

  /* Populate encoder configuration */
  glob->res = vpx_codec_enc_config_default((&vpx_codec_vp8_cx_algo), &glob->cfg, 0);

  if (glob->res)
  {
    dbg_printf("[vp8e - %08lx] Failed to get config: %s\n", (UInt32)glob, vpx_codec_err_to_string(glob->res));
    err = paramErr; //this may be something different ....
    goto bail;
  }

  glob->cfg.g_w = glob->width;
  glob->cfg.g_h = glob->height;
  dbg_printf("[vp8e - %08lx] resolution %dx%d\n", (UInt32)glob,
             glob->cfg.g_w, glob->cfg.g_h);

bail:

  if (err)
    dbg_printf("[vp8e - %08lx] Error %d\n", (UInt32)glob, err);

  if (compressorPixelBufferAttributes) CFRelease(compressorPixelBufferAttributes);

  return err;
}

// Presents the compressor with a frame to encode.
// The compressor may encode the frame immediately or queue it for later encoding.
// If the compressor queues the frame for later decode, it must retain it (by calling ICMCompressorSourceFrameRetain)
// and release it when it is done with it (by calling ICMCompressorSourceFrameRelease).
// Pixel buffers are guaranteed to conform to the pixel buffer options returned by ImageCodecPrepareToCompressFrames.
ComponentResult
VP8_Encoder_EncodeFrame(
                        VP8EncoderGlobals glob,
                        ICMCompressorSourceFrameRef sourceFrame,
                        UInt32 flags)
{
  dbg_printf("[vp8e - %08lx] VP8Encoder_EncodeFrame\n", (UInt32)glob);
  ComponentResult err = noErr;
  ICMCompressionFrameOptionsRef frameOptions;
  dbg_printf("[vp8e - %08lx] flags are %x\n", (UInt32)glob, flags);
  err = encodeThisSourceFrame(glob, sourceFrame);
}

// Directs the compressor to finish with a queued source frame, either emitting or dropping it.
// This frame does not necessarily need to be the first or only source frame emitted or dropped
// during this call, but the compressor must call either ICMCompressorSessionDropFrame or
// ICMCompressorSessionEmitEncodedFrame with this frame before returning.
// The ICM will call this function to force frames to be encoded for the following reasons:
//   - the maximum frame delay count or maximum frame delay time in the sessionOptions
//     does not permit more frames to be queued
//   - the client has called ICMCompressionSessionCompleteFrames.
ComponentResult
VP8_Encoder_CompleteFrame(
                          VP8EncoderGlobals glob,
                          ICMCompressorSourceFrameRef sourceFrame,
                          UInt32 flags)
{
  ComponentResult err = noErr;
  dbg_printf("[vp8e - %08lx] VP8Encoder_CompleteFrame\n", (UInt32)glob);
  ICMCompressionFrameOptionsRef frameOptions;
  dbg_printf("[vp8e - %08lx] flags are %x\n", (UInt32)glob, flags);
  //todo, this should verify that the source frame is complete
  completeThisSourceFrame(glob, sourceFrame);


bail:
  return err;
}



static ICMFrameType
getRequestedFrameType(ICMCompressorSourceFrameRef sourceFrame)
{
  ICMCompressionFrameOptionsRef frameOptions = ICMCompressorSourceFrameGetFrameOptions(sourceFrame);
  ICMFrameType requestedFrameType = frameOptions ? ICMCompressionFrameOptionsGetFrameType(frameOptions) : kICMFrameType_Unknown;
  return requestedFrameType;
}




//These DITL functions are based off of tech notes here http://developer.apple.com/mac/library/technotes/tn2002/tn2081.html


// Item numbers
//
#define kItemOnePass   1
#define kItemTwoPass   2
#define kItemAdvanced  3

pascal ComponentResult VP8_Encoder_GetDITLForSize(VP8EncoderGlobals store,
                                                  Handle *ditl,
                                                  Point *requestedSize)
{
  Handle h = NULL;
  ComponentResult err = noErr;

  switch (requestedSize->h) {
    case kSGSmallestDITLSize:
      GetComponentResource((Component)(store->self), FOUR_CHAR_CODE('DITL'),
                           kVP8_EncoderDITLResID, &h);
      if (NULL != h) *ditl = h;
      else err = resNotFound;
      break;
    default:
      err = badComponentSelector;
      break;
  }

  return err;
}

pascal ComponentResult VP8_Encoder_DITLInstall(VP8EncoderGlobals storage,
                                               DialogRef d,
                                               short itemOffset)
{
  ControlRef cRef;


  unsigned long onePassRadio = (*storage).settings[1] == 1;
  unsigned long twoPassRadio = (*storage).settings[1] == 2;

  GetDialogItemAsControl(d, kItemOnePass + itemOffset, &cRef);
  SetControl32BitValue(cRef, onePassRadio);

  GetDialogItemAsControl(d, kItemTwoPass + itemOffset, &cRef);
  SetControl32BitValue(cRef, twoPassRadio);

  return noErr;
}

pascal ComponentResult VP8_Encoder_DITLEvent(VP8EncoderGlobals storage,
                                             DialogRef d,
                                             short itemOffset,
                                             const EventRecord *theEvent,
                                             short *itemHit,
                                             Boolean *handled)
{
  *handled = false;
  return noErr;
}

pascal ComponentResult VP8_Encoder_DITLItem(VP8EncoderGlobals storage,
                                            DialogRef d,
                                            short itemOffset,
                                            short itemNum)
{
  ControlRef onePassControlRef;
  ControlRef twoPassControlRef;
  GetDialogItemAsControl(d, itemOffset + kItemOnePass, &onePassControlRef);
  GetDialogItemAsControl(d, itemOffset + kItemTwoPass, &twoPassControlRef);


  switch (itemNum - itemOffset) {
    case kItemOnePass:
      SetControl32BitValue(onePassControlRef, 1);
      SetControl32BitValue(twoPassControlRef, 0);
      break;
    case kItemTwoPass:
      SetControl32BitValue(onePassControlRef, 0);
      SetControl32BitValue(twoPassControlRef, 1);
      break;
    case kItemAdvanced:
      runAdvancedWindow(storage);
      break;
  }

  return noErr;
}

pascal ComponentResult VP8_Encoder_DITLRemove(VP8EncoderGlobals storage,
                                              DialogRef d,
                                              short itemOffset)
{
  ControlRef cRef;
  UInt32 onePass;

  GetDialogItemAsControl(d, kItemOnePass + itemOffset, &cRef);
  onePass = GetControl32BitValue(cRef);

  (*storage).settings[1] = onePass?1:2;

  return noErr;
}

pascal ComponentResult VP8_Encoder_DITLValidateInput(VP8EncoderGlobals storage,
                                                     Boolean *ok)
{
  if (ok)
    *ok = true;
  return noErr;
}


ComponentResult VP8_Encoder_GetSettings(VP8EncoderGlobals globals, Handle settings)
{
  ComponentResult err = noErr;

  dbg_printf("[VP8e -- %08lx] GetSettings()\n", (UInt32) globals);

  if (!settings) {
    err = paramErr;
    dbg_printf("[VP8e -- %08lx] ParamErr\n", (UInt32) globals);
  } else {
    SetHandleSize(settings, TOTAL_CUSTOM_VP8_SETTINGS * 4);
    ((UInt32 *) *settings)[0] = 'VP80';
    int i;
    for (i=1;i < TOTAL_CUSTOM_VP8_SETTINGS; i++)
    {
      ((UInt32 *) *settings)[i] = globals->settings[i];
      //dbg_printf("[vp8e] get setting %d as %lu\n",i,((UInt32 *) *settings)[i]);
    }
  }

  return err;
}

ComponentResult VP8_Encoder_SetSettings(VP8EncoderGlobals globals, Handle settings)
{
  ComponentResult err = noErr;
  dbg_printf("[VP8e -- %08lx] SetSettings() %d\n", (UInt32) globals, GetHandleSize(settings));

  int i;
  if (!settings || GetHandleSize(settings) == 0) {
    dbg_printf("[VP8e] no handle\n");
    for (i=1;i< TOTAL_CUSTOM_VP8_SETTINGS; i++)
      globals->settings[i] = UINT_MAX; //default
  }
  else if (GetHandleSize(settings) == TOTAL_CUSTOM_VP8_SETTINGS * 4 && ((UInt32 *) *settings)[0] == 'VP80') {
    for (i=1;i< TOTAL_CUSTOM_VP8_SETTINGS; i++)
    {
      globals->settings[i] = ((UInt32 *) *settings)[i];
    }
  } else {
    dbg_printf("[VP8e] ParamErr\n");
    err = paramErr;
  }

  return err;
}
ComponentResult VP8_Encoder_RequestSettings(VP8EncoderGlobals globals, Handle settings,
                                            Rect *rp, ModalFilterUPP filterProc)
{
  dbg_printf("[VP8e -- %08lx] RequestSettings()\n", (UInt32) globals);
  return badComponentSelector;
}

pascal ComponentResult VP8_Encoder_BeginPass(VP8EncoderGlobals globals,ICMCompressionPassModeFlags  passModeFlags,
                                             UInt32  flags, ICMMultiPassStorageRef  multiPassStorage )
{
  ComponentResult err = noErr;
  dbg_printf("[VP8e -- %08lx] VP8_Encoder_BeginPass(%lu, %lu) \n", (UInt32) globals, passModeFlags,flags);
  if ((passModeFlags &kICMCompressionPassMode_OutputEncodedFrames)
      && !(passModeFlags & kICMCompressionPassMode_ReadFromMultiPassStorage))
  {
    dbg_printf("[VP8e -- %08lx] default 1 pass \n", (UInt32) globals);
    globals->currentPass = VPX_RC_ONE_PASS;
  }
  else if ((passModeFlags & kICMCompressionPassMode_WriteToMultiPassStorage) &&
           !(passModeFlags & kICMCompressionPassMode_OutputEncodedFrames))
  {
    dbg_printf("[VP8e -- %08lx] First Pass \n", (UInt32) globals);
    if (globals->stats.buf != NULL)
    {
      free(globals->stats.buf);
      globals->stats.buf =NULL;
      globals->stats.sz=0;
    }
    globals->currentPass = VPX_RC_FIRST_PASS;
  }
  else if ((passModeFlags & kICMCompressionPassMode_OutputEncodedFrames)
           && (passModeFlags & kICMCompressionPassMode_ReadFromMultiPassStorage))
  {
    dbg_printf("[VP8e -- %08lx] Second Pass \n", (UInt32) globals);
    globals->currentPass = VPX_RC_LAST_PASS;
    if (globals->codec == NULL) // this should be initialized if there was a first pass
      return nilHandleErr;
    globals->cfg.g_pass = VPX_RC_LAST_PASS;
    globals->cfg.rc_twopass_stats_in.sz = globals->stats.sz;
    globals->cfg.rc_twopass_stats_in.buf = globals->stats.buf;
    globals->frameCount = 0;
    if(vpx_codec_enc_init(globals->codec,  &vpx_codec_vp8_cx_algo, &globals->cfg, 0))
    {
      const char *detail = vpx_codec_error_detail(globals->codec);
      dbg_printf("[VP8e] Failed to initialize encoder second pass %s\n", detail);
      return notOpenErr;
    }
    setCustomPostInit(globals); //not sure if I this is needed just following ivfenc example
  }
  else
  {
    return paramErr;///not sure what other type of pass there is
  }

  return err;
}
pascal ComponentResult VP8_Encoder_EndPass(VP8EncoderGlobals globals)
{
  ComponentResult err = noErr;
  dbg_printf("[VP8e -- %08lx] VP8_Encoder_EndPass(%lu, %lu) \n", (UInt32) globals);
  if (globals->currentPass == VPX_RC_FIRST_PASS)
  {
    unsigned int prevStatsSize = 0;
    while (globals->stats.sz != prevStatsSize)
    {
      prevStatsSize = globals->stats.sz;
      //send a null frame to encode frame, this ends off the encoder stats
      encodeThisSourceFrame(globals, NULL);
    }
    //reset all my stats
    globals->frameCount =0;
  }
  //I don't need to do anything here currently for the second pass
  return err;
}

pascal ComponentResult VP8_Encoder_ProcessBetweenPasses(VP8EncoderGlobals globals, ICMMultiPassStorageRef  multiPassStorage,
                                                        Boolean * interpassProcessingDoneOut,
                                                        ICMCompressionPassModeFlags * requestedNextPassModeFlagsOut)
{
  ComponentResult err = noErr;
  dbg_printf("[VP8e -- %08lx] VP8_Encoder_ProcessBetweenPasses \n", (UInt32) globals);
  //I don't need to do anything here currently
  return err;
}
