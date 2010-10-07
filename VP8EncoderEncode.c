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

static void setCustom(VP8EncoderGlobals glob);
static ComponentResult setMaxKeyDist(VP8EncoderGlobals glob);
static ComponentResult setFrameRate(VP8EncoderGlobals glob);
static ComponentResult setBitrate(VP8EncoderGlobals glob,
                                  ICMCompressorSourceFrameRef sourceFrame);
static void setUInt(unsigned int * i, UInt32 val);
static void setCustom(VP8EncoderGlobals glob);
static void initializeCodec(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);
static void converColorSpace(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);

//initialize the codec if needed
static void initializeCodec(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame)
{
    if (glob->codec != NULL)
        return;
    glob->codec = calloc(1, sizeof(vpx_codec_ctx_t));
    setBitrate(glob, sourceFrame); //because we don't know framerate untile we have a source image.. this is done here
    setMaxKeyDist(glob);
    setFrameRate(glob);
    setCustom(glob);
    glob->cfg.g_pass = glob->currentPass; 
                        
    if (vpx_codec_enc_init(glob->codec, &vpx_codec_vp8_cx_algo, &glob->cfg, 0))
    {
        const char *detail = vpx_codec_error_detail(glob->codec);
        dbg_printf("[vp8e - %08lx] Failed to initialize encoder pass = %d %s\n", (UInt32)glob, glob->currentPass, detail);
    }
    setCustomPostInit(glob);
}

static ComponentResult convertColorSpace(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame)
{
    CVPixelBufferRef sourcePixelBuffer = NULL;
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
    ComponentResult err = CopyChunkyYUV422ToPlanarYV12(glob->width, glob->height,
                                       CVPixelBufferGetBaseAddress(sourcePixelBuffer),
                                       CVPixelBufferGetBytesPerRow(sourcePixelBuffer),
                                       glob->raw->planes[PLANE_Y],
                                       glob->raw->stride[PLANE_Y],
                                       glob->raw->planes[PLANE_U],
                                       glob->raw->stride[PLANE_U],
                                       glob->raw->planes[PLANE_V],
                                       glob->raw->stride[PLANE_V]);
    
    CVPixelBufferUnlockBaseAddress(sourcePixelBuffer, 0);    
    dbg_printf("[vp8e - %08lx]  CVPixelBufferUnlockBaseAddress %x\n", sourcePixelBuffer);    
    
    return err;
}
                     
ComponentResult encodeThisSourceFrame(VP8EncoderGlobals glob,
                                      ICMCompressorSourceFrameRef sourceFrame)
{
    vpx_codec_err_t codecError;
    ComponentResult err = noErr;
    ICMMutableEncodedFrameRef encodedFrame = NULL;
    unsigned char *dataPtr;
    const UInt8 *decoderDataPtr;
    size_t dataSize = 0;
    MediaSampleFlags mediaSampleFlags;
    int storageIndex = 0;
    
    dbg_printf("[vp8e - %08lx] encode this frame %08lx\n", (UInt32)glob, (UInt32)sourceFrame);
    
    //long dispNumber = ICMCompressorSourceFrameGetDisplayNumber(sourceFrame);
    
    
    // Initialize codec if needed
    initializeCodec(glob, sourceFrame);
    
    ///////         Transfer the current frame to glob->raw
    if (sourceFrame != NULL)
    {
        err = convertColorSpace(glob, sourceFrame);
        if (err) goto bail;
        int flags = 0 ; //TODO - find out what I may need in these flags
        dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec %x  raw %x framecount %d  flags %x\n", (UInt32)glob, glob->codec, glob->raw, glob->frameCount,  flags);
        codecError = vpx_codec_encode(glob->codec, glob->raw, glob->frameCount,
                                      1, flags, VPX_DL_GOOD_QUALITY);
        dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec exit\n", (UInt32)glob);
    }
    else  //sourceFrame is Null. this could be termination of a pass 
    {
        int flags = 0 ; //TODO - find out what I may need in these flags
        dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec %x  raw %x framecount %d ----NULL TERMINATION\n", (UInt32)glob, glob->codec, NULL, glob->frameCount,  flags);
        codecError = vpx_codec_encode(glob->codec, NULL, glob->frameCount,
                                      1, flags, VPX_DL_GOOD_QUALITY);
    }
    
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
                if (encodedFrame == NULL)
                {
                    err = ICMEncodedFrameCreateMutable(glob->session, sourceFrame, 
                                                       glob->maxEncodedDataSize, &encodedFrame);
                    dataPtr = ICMEncodedFrameGetDataPtr(encodedFrame);
                    dbg_printf("[vp8e - %08lx] getDataPtr %x\n", (UInt32)glob, dataPtr);
                }
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
            case VPX_CODEC_STATS_PKT:
                if (1)
                {
                    unsigned long newSize = glob->stats.sz + pkt->data.twopass_stats.sz;
                    glob->stats.buf = realloc(glob->stats.buf, newSize);
                    if (!glob->stats.buf)
                        return mFulErr;
                    dbg_printf("[vp8e - %08lx] Reallocation buffer size to %ld\n", (UInt32)glob, newSize);
                    memcpy((char*)glob->stats.buf + glob->stats.sz, pkt->data.twopass_stats.buf,
                           pkt->data.twopass_stats.sz);
                    glob->stats.sz = newSize;
                }
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
    
    if (glob->currentPass == VPX_RC_FIRST_PASS)
    {
        //in the first pass no need to export any frames
        return err;  
    }
    
    if (encodedFrame == NULL)
    {
        ICMCompressorSessionDropFrame(glob->session,sourceFrame);
        return err;          
    }
    
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
        //TODO I want this to be bits per pixel
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

void setCustomPostInit(VP8EncoderGlobals glob)
{
    if (glob->settings[12] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_CPUUSED, glob->settings[12]);
    if (glob->settings[13] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_NOISE_SENSITIVITY, glob->settings[13]);
    if (glob->settings[14] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_SHARPNESS, glob->settings[14]);
    if (glob->settings[15] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_STATIC_THRESHOLD, glob->settings[15]);
    //setUIntPostInit(glob, VP8E_SET_TOKEN_PARTITIONS, 25);  // TODO not sure how to set this.
    
    //TODO verify this when enabling alt - ref
    if (glob->settings[25] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_ENABLEAUTOALTREF, glob->settings[25]);
    if (glob->settings[26] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_ARNR_MAXFRAMES, glob->settings[26]);
    if (glob->settings[27] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_ARNR_STRENGTH, glob->settings[27]);
    if (glob->settings[28] != UINT_MAX)
        vpx_codec_control(glob->codec, VP8E_SET_ARNR_TYPE, glob->settings[28]);
}

