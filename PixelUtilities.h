// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


/*

File: PixelUtilities.h, part of ExampleIPBCodec

Abstract: Utilities for converting between chunky YUV 4:2:2 and planar YUV 4:2:0.

Version: 1.0

� Copyright 2005 Apple Computer, Inc. All rights reserved.

IMPORTANT:  This Apple software is supplied to
you by Apple Computer, Inc. ("Apple") in
consideration of your agreement to the following
terms, and your use, installation, modification
or redistribution of this Apple software
constitutes acceptance of these terms.  If you do
not agree with these terms, please do not use,
install, modify or redistribute this Apple
software.

In consideration of your agreement to abide by
the following terms, and subject to these terms,
Apple grants you a personal, non-exclusive
license, under Apple's copyrights in this
original Apple software (the "Apple Software"),
to use, reproduce, modify and redistribute the
Apple Software, with or without modifications, in
source and/or binary forms; provided that if you
redistribute the Apple Software in its entirety
and without modifications, you must retain this
notice and the following text and disclaimers in
all such redistributions of the Apple Software.
Neither the name, trademarks, service marks or
logos of Apple Computer, Inc. may be used to
endorse or promote products derived from the
Apple Software without specific prior written
permission from Apple.  Except as expressly
stated in this notice, no other rights or
licenses, express or implied, are granted by
Apple herein, including but not limited to any
patent rights that may be infringed by your
derivative works or by other works in which the
Apple Software may be incorporated.

The Apple Software is provided by Apple on an "AS
IS" basis.  APPLE MAKES NO WARRANTIES, EXPRESS OR
IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING
THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE
OR IN COMBINATION WITH YOUR PRODUCTS.

IN NO EVENT SHALL APPLE BE LIABLE FOR ANY
SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF
THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER
UNDER THEORY OF CONTRACT, TORT (INCLUDING
NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN
IF APPLE HAS BEEN ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.

*/

#ifndef PIXELUTILITIES_H
#define PIXELUTILITIES_H

#include <QuickTime/QuickTime.h>

// Our YUV 4:2:2 format, known as k422YpCbCr8CodecType or '2vuy', is ordered Y0, Cb, Y1, Cr.
// These utilities convert between it and a simple planar YUV 4:2:0.
// These utilities assume that width and height are both multiples of 2.

extern OSStatus CopyChunkyYUV422ToPlanarYUV420(
    size_t width,
    size_t height,
    const unsigned char *baseAddr_2vuy,
    int rowBytes_2vuy,
    unsigned char *baseAddr_y,
    int rowBytes_y,
    unsigned char *baseAddr_u,
    int rowBytes_u,
    unsigned char *baseAddr_v,
    int rowBytes_v);

extern OSStatus CopyPlanarYUV420ToChunkyYUV422(
    size_t width,
    size_t height,
    const UInt8 *baseAddr_y,
    size_t rowBytes_y,
    const UInt8 *baseAddr_u,
    size_t rowBytes_u,
    const UInt8 *baseAddr_v,
    size_t rowBytes_v,
    UInt8 *baseAddr_2vuy,
    size_t rowBytes_2vuy);


extern OSStatus DebugAllBlackYV12(
    size_t width,
    size_t height,
    const unsigned char *baseAddr_2vuy,
    int rowBytes_2vuy,
    unsigned char *baseAddr_y,
    int rowBytes_yv12);


extern OSStatus CopyChunkyYUV422ToPlanarYV12(
    size_t width,
    size_t height,
    const unsigned char *baseAddr_2vuy,
    int rowBytes_2vuy,
    unsigned char *baseAddr_y,
    int rowBytes_y,
    unsigned char *baseAddr_u,
    int rowBytes_u,
    unsigned char *baseAddr_v,
    int rowBytes_v);

extern OSStatus CopyPlanarYV12ToChunkyYUV422(
    size_t width,
    size_t height,
    UInt8 *baseAddr_y,
    size_t rowBytes_y,
    UInt8 *baseAddr_u,
    size_t rowBytes_u,
    UInt8 *baseAddr_v,
    size_t rowBytes_v,
    UInt8 *baseAddr_2vuy,
    size_t rowBytes_2vuy);
#endif // PIXELUTILITIES_H
