// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



#ifndef LOG_H
#define LOG_H


#include <QuickTime/QuickTime.h>


void log_time(FILE *logFile, const char *id, const char *fmt, ...);
void dbg_printf(const char *s, ...)  __attribute__((format(printf, 1, 2)));
void dbg_dumpBytes(unsigned char *bytes, int size);
void dbg_dumpAtom(QTAtomContainer container);


#endif

