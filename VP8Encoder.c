
// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



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
#define HAVE_CONFIG_H "vpx_codecs_config.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vpx_codec_impl_top.h"
#include "vpx/vpx_codec_impl_bottom.h"
#include "vpx/vp8cx.h"
#include "VP8Encoder.h"
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

// Prototypes for local utility functions defined later:
static ComponentResult encodeSomeSourceFrames(VP8EncoderGlobals glob);
static ComponentResult encodeThisSourceFrame(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);

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
    glob->currentPass = kICMCompressionPassMode_OutputEncodedFrames;
    
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
        
        if (glob->stats.buf != NULL)
        {
            free(glob->stats.buf);
            glob->stats.buf =NULL;
            glob->stats.sz=0;
        }
            

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

static ComponentResult setMaxKeyDist(VP8EncoderGlobals glob)
{
    SInt32 maxInterval = 0;
    ComponentResult err = ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                          kQTPropertyClass_ICMCompressionSessionOptions,
                          kICMCompressionSessionOptionsPropertyID_MaxKeyFrameInterval,
                          sizeof(SInt32),
                          &maxInterval, NULL);

    if (err) return err;

    if (maxInterval == 0)
        glob->cfg.kf_max_dist = 300;  //default : don't pass in 0 as vp8 sdk reserves that for key every frame
    else
        glob->cfg.kf_max_dist = maxInterval;

    dbg_printf("[vp8e - %08lx] setMaxKeyDist %ld\n", (UInt32)glob, glob->cfg.kf_max_dist);
    return noErr;
}

static ComponentResult setFrameRate(VP8EncoderGlobals glob)
{
    dbg_printf("[vp8e - %08lx] setFrameRate \n", (UInt32) glob);
    ComponentResult err = noErr;
    Fixed frameRate;
    err= ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                                                 kQTPropertyClass_ICMCompressionSessionOptions,
                                                 kICMCompressionSessionOptionsPropertyID_ExpectedFrameRate,
                                                 sizeof(Fixed), &frameRate, NULL);
    if (err) goto bail;
    double fps = FixedToFloat(frameRate);
    dbg_printf("[vp8e - %08lx] Got FrameRate %f \n", (UInt32) glob,fps);
    if (fps == 0)
        return err; //use the defaults
    if (fps > 29.965 && fps < 29.975) // I am putting everything in this threshold as 29.97
    {
        glob->cfg.g_timebase.num = 1001;
        glob->cfg.g_timebase.den = 30000;
    }
    else 
    {
        //I'm using a default of a millisecond timebase, this may not be 100% accurate
        // however, container uses this timebase so this estimate is best.
        glob->cfg.g_timebase.num = 1000;
        glob->cfg.g_timebase.den = fps * 1000;
    }
    dbg_printf("[vp8e - %08lx] Setting g_timebase to %d/%d \n", (UInt32) glob, 
               glob->cfg.g_timebase.num, glob->cfg.g_timebase.den);

bail:
    return err;
}

static ComponentResult setBitrate(VP8EncoderGlobals glob,
                                  ICMCompressorSourceFrameRef sourceFrame)
{
    ComponentResult err = noErr;
    TimeValue64 frameDisplayDuration = 0;
    TimeScale timescale = 0;
    ICMValidTimeFlags validTimeFlags = 0;
    unsigned int bitrate = 0;

    // If we have a known frame duration and an average data rate, use them to guide the byte budget.
    err = ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(sourceFrame, NULL, &frameDisplayDuration,
            &timescale, &validTimeFlags);
    /* Update the default configuration with our settings */
    SInt32 avgDataRate = 0;
    err = ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
            kQTPropertyClass_ICMCompressionSessionOptions,
            kICMCompressionSessionOptionsPropertyID_AverageDataRate,
            sizeof(avgDataRate), &avgDataRate, NULL);

    if (avgDataRate != 0 )
    {
        //convert from bytes/sec to kilobits/second
        bitrate = avgDataRate * 8 /1000;
        dbg_printf("[vp8e - %08lx] setting bitrate to %d (from averageDataRate)\n", (UInt32)glob, bitrate);
    }
    else
    {
        CodecQ sliderVal = codecNormalQuality; //min = 0 max = 1024
        err = ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                kQTPropertyClass_ICMCompressionSessionOptions,
                kICMCompressionSessionOptionsPropertyID_Quality,
                sizeof(CodecQ), &sliderVal, NULL);

        if (err) return err;

        if (sliderVal > codecMaxQuality)
            sliderVal = codecMaxQuality;
        //I want this to be bits per pixel
        double totalPixels = glob->width * glob->height;
        double tmp = totalPixels * sliderVal / (640.0 * 480.0);
        bitrate = tmp+ 10;
        dbg_printf("[vp8e - %08lx] setting bitrate to %d (calculated from Quality Slider)\n", (UInt32)glob, bitrate);
    }

    glob->cfg.rc_target_bitrate = bitrate;
}


static void setUInt(unsigned int * i, UInt32 val)
{
    if (val == UINT_MAX)
        return;
    *i= val;
}

static void setCustom(VP8EncoderGlobals glob)
{
    setUInt(&glob->cfg.g_threads, glob->settings[2]);
    setUInt(&glob->cfg.g_error_resilient, glob->settings[3]);
    setUInt(&glob->cfg.rc_dropframe_thresh, glob->settings[4]);
    if(glob->settings[5] == 1)
        glob->cfg.rc_end_usage = VPX_CBR;
    else if (glob->settings[5] ==2)
        glob->cfg.rc_end_usage = VPX_VBR;
    setUInt(&glob->cfg.g_lag_in_frames, glob->settings[6]);

    setUInt(&glob->cfg.rc_min_quantizer, glob->settings[8]);
    setUInt(&glob->cfg.rc_max_quantizer, glob->settings[9]);
    setUInt(&glob->cfg.rc_undershoot_pct, glob->settings[10]);
    setUInt(&glob->cfg.rc_overshoot_pct, glob->settings[11]);
    
    setUInt(&glob->cfg.rc_resize_allowed, glob->settings[16]);
    setUInt(&glob->cfg.rc_resize_up_thresh, glob->settings[17]);
    setUInt(&glob->cfg.rc_resize_down_thresh, glob->settings[18]);
    setUInt(&glob->cfg.rc_buf_sz, glob->settings[19]);
    setUInt(&glob->cfg.rc_buf_initial_sz, glob->settings[20]);
    setUInt(&glob->cfg.rc_buf_optimal_sz, glob->settings[21]);
    
    if(glob->settings[22] == 1)
        glob->cfg.kf_mode = VPX_KF_DISABLED;
    if(glob->settings[22] == 2)
        glob->cfg.kf_mode = VPX_KF_AUTO;
    
    setUInt(&glob->cfg.kf_min_dist, glob->settings[23]);
    setUInt(&glob->cfg.kf_max_dist, glob->settings[24]);

    setUInt(&glob->cfg.rc_2pass_vbr_bias_pct, glob->settings[30]);
    setUInt(&glob->cfg.rc_2pass_vbr_minsection_pct, glob->settings[31]);
    setUInt(&glob->cfg.rc_2pass_vbr_maxsection_pct, glob->settings[32]);

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

    err = encodeThisSourceFrame(glob, sourceFrame);

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


static ComponentResult
encodeThisSourceFrame(
    VP8EncoderGlobals glob,
    ICMCompressorSourceFrameRef sourceFrame)
{
    ComponentResult err = noErr;
    ICMMutableEncodedFrameRef encodedFrame = NULL;
    unsigned char *dataPtr;
    const UInt8 *decoderDataPtr;
    size_t dataSize = 0;
    MediaSampleFlags mediaSampleFlags;
    CVPixelBufferRef sourcePixelBuffer = NULL;
    int storageIndex = 0;

    dbg_printf("[vp8e - %08lx] encode this frame\n", (UInt32)glob);

    long dispNumber = ICMCompressorSourceFrameGetDisplayNumber(sourceFrame);
    // Create the buffer for the encoded frame.
    err = ICMEncodedFrameCreateMutable(glob->session, sourceFrame, glob->maxEncodedDataSize, &encodedFrame);

    if (err)
        goto bail;

    dbg_printf("[vp8e - %08lx] created the buffer for the encoded frame\n", (UInt32)glob);

    /* Initialize codec */
    if (glob->codec == NULL)
    {
        glob->codec = calloc(1, sizeof(vpx_codec_ctx_t));
        setBitrate(glob, sourceFrame); //because we don't know framerate untile we have a source image.. this is done here
        setMaxKeyDist(glob);
        setFrameRate(glob);
        setCustom(glob);

        if (vpx_codec_enc_init(glob->codec, &vpx_codec_vp8_cx_algo, &glob->cfg, 0))
            dbg_printf("[vp8e - %08lx] Failed to initialize encoder\n", (UInt32)glob);
    }

    dataPtr = ICMEncodedFrameGetDataPtr(encodedFrame);
    dbg_printf("[vp8e - %08lx] getDataPtr %x\n", (UInt32)glob, dataPtr);

    ///////         Transfer the current frame to glob->raw
    sourcePixelBuffer = ICMCompressorSourceFrameGetPixelBuffer(sourceFrame);
    CVPixelBufferLockBaseAddress(sourcePixelBuffer, 0);
    //copy our frame to the raw image.  TODO: I'm not checking for any padding here.
    unsigned char *srcBytes = CVPixelBufferGetBaseAddress(sourcePixelBuffer);
    dbg_printf("[vp8e - %08lx] CVPixelBufferGetBaseAddress %x\n", (UInt32)glob, sourcePixelBuffer);
    dbg_printf("[vp8e - %08lx] CopyChunkyYUV422ToPlanarYV12 %dx%d, %x, %d, %x, %d, %x, %d, %x, %d \n", (UInt32)glob,
               glob->width, glob->height,
               CVPixelBufferGetBaseAddress(sourcePixelBuffer),
               CVPixelBufferGetBytesPerRow(sourcePixelBuffer),
               glob->raw->planes[PLANE_Y],
               glob->raw->stride[PLANE_Y],
               glob->raw->planes[PLANE_U],
               glob->raw->stride[PLANE_U],
               glob->raw->planes[PLANE_V],
               glob->raw->stride[PLANE_V]);
    err = CopyChunkyYUV422ToPlanarYV12(glob->width, glob->height,
                                       CVPixelBufferGetBaseAddress(sourcePixelBuffer),
                                       CVPixelBufferGetBytesPerRow(sourcePixelBuffer),
                                       glob->raw->planes[PLANE_Y],
                                       glob->raw->stride[PLANE_Y],
                                       glob->raw->planes[PLANE_U],
                                       glob->raw->stride[PLANE_U],
                                       glob->raw->planes[PLANE_V],
                                       glob->raw->stride[PLANE_V]);

    CVPixelBufferUnlockBaseAddress(sourcePixelBuffer, 0);    //TODO here is unlock, where is lock?
    dbg_printf("[vp8e - %08lx]  CVPixelBufferUnlockBaseAddress %x\n", sourcePixelBuffer);
    int flags = 0 ; //TODO - find out what I may need in these flags
    dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec %x  raw %x framecount %d  flags %x\n", (UInt32)glob, glob->codec, glob->raw, glob->frameCount,  flags);
    vpx_codec_err_t codecError = vpx_codec_encode(glob->codec, glob->raw, glob->frameCount,
                                 1, flags, VPX_DL_GOOD_QUALITY);
    dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec exit\n", (UInt32)glob);

    if (codecError)
    {
        const char *detail = vpx_codec_error_detail(glob->codec);
        dbg_printf("[vp8e - %08lx]  error vpx encode is %s\n", (UInt32)glob, vpx_codec_error(glob->codec));

        if (detail)
            dbg_printf("    %s\n", detail);

        goto bail;
    }

    vpx_codec_iter_t iter = NULL;
    int got_data = 0;
    Boolean keyFrame = false;
    Boolean droppableFrame = false;

    while (1)
    {
        const vpx_codec_cx_pkt_t *pkt = vpx_codec_get_cx_data(glob->codec, &iter);

        if (pkt == NULL)
            break;

        got_data ++;

        switch (pkt->kind)
        {
        case VPX_CODEC_CX_FRAME_PKT:

            //paranoid check to make sure I don't write past my buffer
            if (pkt->data.frame.sz + dataSize >=  glob->maxEncodedDataSize)
            {
                dbg_printf("[vp8e - %08lx] Error: buffer overload.  Encoded frame larger than raw frame\n", (UInt32)glob);
                goto bail;
            }

            dbg_printf("[vp8e - %08lx] copying %d bytes of data to output dataBuffer\n", (UInt32)glob, pkt->data.frame.sz);
            memcpy(&(dataPtr[dataSize]), pkt->data.frame.buf, pkt->data.frame.sz);
            dataSize += pkt->data.frame.sz;
            break;
        default:
            break;
        }

        keyFrame = pkt->kind == VPX_CODEC_CX_FRAME_PKT
                   && (pkt->data.frame.flags & VPX_FRAME_IS_KEY);
        dbg_printf(keyFrame ? "Key Packet\n" : "Non Key Packet\n");
        droppableFrame = pkt->data.frame.flags & VPX_FRAME_IS_DROPPABLE;
        dbg_printf(droppableFrame ? "Droppable frame\n" : "Not droppable frame\n");
    }

    if (got_data == 0)
        dbg_printf("[vp8e - %08lx] Warning: No data generated for this frame\n", (UInt32)glob);
    else
    {
        dbg_printf("[vp8e - %08lx]  Encoded frame %d with %d bytes of data  -- got_data packets %d\n", (UInt32)glob, glob->frameCount, dataSize, got_data);
    }

    glob->frameCount++ ;

    // Update the encoded frame to reflect the actual frame size, sample flags and frame type.
    err = ICMEncodedFrameSetDataSize(encodedFrame, dataSize);

    if (err) goto bail;

    mediaSampleFlags = 0;

    if (! keyFrame)
    {
        mediaSampleFlags |= mediaSampleNotSync;

        if (droppableFrame)
            mediaSampleFlags |= mediaSampleDroppable;
    }

    ICMFrameType frameType = kICMFrameType_P;

    if (keyFrame)
    {
        frameType = kICMFrameType_I;
        dbg_printf("[vp8e - %08lx] frame type set to I", (UInt32)glob);
    }
    else
    {
        dbg_printf("[vp8e - %08lx] frame type set to P", (UInt32)glob);
    }

    if (kICMFrameType_I == frameType)
        mediaSampleFlags |= mediaSampleDoesNotDependOnOthers;

    err = ICMEncodedFrameSetMediaSampleFlags(encodedFrame, mediaSampleFlags);

    if (err)
        goto bail;

    err = ICMEncodedFrameSetFrameType(encodedFrame, frameType);

    if (err)
        goto bail;

    // Output the encoded frame.
    dbg_printf("[vp8e - %08lx]  Emit Encoded Frames\n", (UInt32)glob);
    err = ICMCompressorSessionEmitEncodedFrame(glob->session, encodedFrame, 1, &sourceFrame);

    if (err)
        goto bail;

bail:

    if (err)
        dbg_printf("[vp8e - %08lx]  bailed with err %d\n", (UInt32)glob, err);

    // Since we created this, we must also release it.
    ICMEncodedFrameRelease(encodedFrame);

    return err;
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
    if (passModeFlags == kICMCompressionPassMode_OutputEncodedFrames)
    {
        //default 1 pass
        globals->currentPass = passModeFlags;
    }
    if (passModeFlags == kICMCompressionPassMode_WriteToMultiPassStorage)
    {
        //doing a first pass
        if (globals->stats.buf != NULL)
        {
            free(globals->stats.buf);
            globals->stats.buf =NULL;
            globals->stats.sz=0;
        }
        globals->currentPass = passModeFlags;
    }
    else if (passModeFlags == kICMCompressionPassMode_ReadFromMultiPassStorage)
    {
        //doing a second pass
        globals->currentPass = passModeFlags;        
    }
    else 
    {
        return paramErr;/// its asking me to do something I don't know how to do
    }

    
    return err;
}
pascal ComponentResult VP8_Encoder_EndPass(VP8EncoderGlobals globals)
{
    ComponentResult err = noErr;
    dbg_printf("[VP8e -- %08lx] VP8_Encoder_EndPass(%lu, %lu) \n", (UInt32) globals);
    //I don't need to do anything here currently
    return err;
}