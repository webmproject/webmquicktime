// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



#ifndef EBMLBUFFERWRITER_HPP
#define EBMLBUFFERWRITER_HPP

typedef struct
{
    wide offset;
} EbmlLoc;

typedef struct
{
    DataHandler data_h;
    wide offset;
} EbmlGlobal;


void Ebml_StartSubElement(EbmlGlobal *glob, EbmlLoc *ebmlLoc, unsigned long class_id);
void Ebml_EndSubElement(EbmlGlobal *glob,  EbmlLoc *ebmlLoc);
void Ebml_GetEbmlLoc(EbmlGlobal *glob,  EbmlLoc *ebmlLoc);
void Ebml_SetEbmlLoc(EbmlGlobal *glob,  EbmlLoc *ebmlLoc);

#endif
