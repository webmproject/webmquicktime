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
#include "Raw_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

void writeRaw(const char *fname, size_t length, const unsigned char *bytes)
{
#if WRITE_RAW
    FILE *f = fopen(fname, "w");
    fwrite(bytes, 1, length, f);
    fclose(f);
#endif
}

void appendRaw(const char *fname, size_t length, const unsigned char *bytes)
{
#if WRITE_RAW
    FILE *f = fopen(fname, "a");
    fwrite(bytes, 1, length, f);
    fclose(f);
#endif
}

