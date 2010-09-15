
// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
#ifndef _VP8ENCODER_GUI_H_
#define _VP8ENCODER_GUI_H_

#define kDEADLINE 'VPdl'
#define kTHREADCOUNT 'VPtc' 
#define kERROR_RESILIENT 'VPer'
#define kEND_USAGE 'VPeu'
#define kLOG_IN_FRAMES 'VPlf'
#define kTOKEN_PARTITIONS 'VPtp'
#define kMIN_QUANTIZERS  'VPmq'
#define kMAX_QUANTIZERS 'VPMq'
#define kUNDERSHOOT_PCT 'VPup'
#define kOVERSHOOT_PCT 'VPop'
#define kCPU_USAGE 'VPcu'
#define kNOISE_SENSITIVITY 'VPns'
#define kSHARPNESS 'VPs '
#define kMOTION_DETECTION_THRESHOLD 'VPmd'
#define kSPATIAL_RESAMPLING_ALLOWED 'VPsr'
#define kUP_THRESHOLD 'VPut'
#define kDOWN_THRESHOLD 'VPdt'
#define kDECODE_BUFFER_SIZE 'VPds'
#define kINITIAL_SIZE 'VPdi'
#define kOPTIMAL_SIZE 'VPdo'
#define kKEYFRAME_MODE 'VPkm'
#define kMIN_INTERVAL 'VPmi'
#define kMAX_INTERVAL 'VPMi'
#define kALT_REF_ENABLE 'VPar'
#define kALT_REF_MAX_FRAMES 'VPam'
#define kALT_REF_STRENGTH 'VPas'
#define kALT_REF_TYPE 'VPat'




ComponentResult runAdvancedWindow(VP8EncoderGlobals globals);

#endif