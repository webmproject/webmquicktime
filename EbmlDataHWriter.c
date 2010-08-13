// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include <QuickTime/QuickTime.h>
#include "EbmlDataHWriter.h"

#include "EbmlWriter.h"
//#include <cassert>
//#include <limits>
//#include <malloc.h>  //_alloca
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

#include "log.h";

static void assignWide(wide *dest, wide *src)
{
    dest->hi = src->hi;
    dest->lo = src->lo;
}

static void addToWide(wide *w, long val)
{
    wide tmp;
    tmp.hi = 0;
    tmp.lo = val;

    WideAdd(w, &tmp);
}


void Ebml_Write(EbmlGlobal *glob, const void *buffer_in, unsigned long len)
{
    ComponentResult cResult = DataHWrite64(glob->data_h, (void *)buffer_in, &glob->offset, len, NULL, 0);
    addToWide(&glob->offset, len);
}

static void _Serialize(EbmlGlobal *glob, const unsigned char *p, const unsigned char *q)
{
    ComponentResult cResult;

    while (q != p)
    {
        --q;

        unsigned long cbWritten;
        DataHWrite64(glob->data_h, (void *)q, &glob->offset, 1, NULL, 0);
        addToWide(&glob->offset, 1);
    }
}

void Ebml_Serialize(EbmlGlobal *glob, const void *buffer_in, unsigned long len)
{
    //assert(buf);

    const unsigned char *const p = (const unsigned char *)(buffer_in);
    const unsigned char *const q = p + len;

    _Serialize(glob, p, q);
}


void Ebml_StartSubElement(EbmlGlobal *glob, EbmlLoc *ebmlLoc, unsigned long class_id)
{
    Ebml_WriteID(glob, class_id);
    assignWide(&ebmlLoc->offset, &glob->offset);
    //TODO this is always taking 8 bytes, this may need later optimization
    unsigned long long unknownLen =  0x01FFFFFFFFFFFFFFLLU;
    Ebml_Serialize(glob, (void *)&unknownLen, 8); //this is a key that says length unknown
}

void Ebml_EndSubElement(EbmlGlobal *glob, EbmlLoc *ebmlLoc)
{
    wide currentEndOfFile;
    assignWide(&currentEndOfFile, &glob->offset);

    UInt64 sizeOfElement = *(SInt64 *)&currentEndOfFile - *(SInt64 *)&ebmlLoc->offset -8;

    assignWide(&glob->offset, &ebmlLoc->offset);
    sizeOfElement |=  0x0100000000000000LLU;
    Ebml_Serialize(glob, (void *)&sizeOfElement, 8);
    assignWide(&glob->offset, &currentEndOfFile);
}

void Ebml_GetEbmlLoc(EbmlGlobal *glob,  EbmlLoc *ebmlLoc)
{
    assignWide(&ebmlLoc->offset, &glob->offset);
}

void Ebml_SetEbmlLoc(EbmlGlobal *glob,  EbmlLoc *ebmlLoc)
{
    assignWide(&glob->offset, &ebmlLoc->offset);
}
