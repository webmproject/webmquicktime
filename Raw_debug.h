// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



#ifndef IVF_DEBUG_H
#define IVF_DEBUG_H

//#define WRITE_RAW 1  //defined in configuration now


void writeRaw(const char *fname, size_t length, const unsigned char *bytes);
void appendRaw(const char *fname, size_t length, const unsigned char *bytes);
#endif