// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#ifndef __VP8ENCODER_H__
#define __VP8ENCODER_H__
#define kVP8_EncoderDITLResID 129

//NOTE: the last custom setting is the 2 pass custom setting
#define TOTAL_CUSTOM_VP8_SETTINGS 30

typedef UInt32 VP8customSettings[TOTAL_CUSTOM_VP8_SETTINGS];

typedef struct
{
    ComponentInstance               self;
    ComponentInstance               target;
    
    ICMCompressorSessionRef         session; // NOTE: we do not need to retain or release this
    ICMCompressionSessionOptionsRef sessionOptions;
    
    long                            width;
    long                            height;
    size_t                          maxEncodedDataSize;
    int                             nextDecodeNumber;
    
    //VP8 Specific Variables
    vpx_codec_err_t      res;
    vpx_codec_ctx_t      *codec;
    vpx_codec_enc_cfg_t  cfg;
    vpx_image_t          *raw;
    VP8customSettings    settings;
    int                  frameCount;
    
} VP8EncoderGlobalsRecord, *VP8EncoderGlobals;

#endif