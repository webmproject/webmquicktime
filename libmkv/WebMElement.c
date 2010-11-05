// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include "EbmlBufferWriter.h"
#include "EbmlIDs.h"
#include "WebMElement.h"
#include <stdio.h>

#define kVorbisPrivateMaxSize  4000

void writeHeader(EbmlGlobal *glob)
{
  EbmlLoc start;
  Ebml_StartSubElement(glob, &start, EBML);
  Ebml_SerializeUnsigned(glob, EBMLVersion, 1);
  Ebml_SerializeUnsigned(glob, EBMLReadVersion, 1); //EBML Read Version
  Ebml_SerializeUnsigned(glob, EBMLMaxIDLength, 4); //EBML Max ID Length
  Ebml_SerializeUnsigned(glob, EBMLMaxSizeLength, 8); //EBML Max Size Length
  Ebml_SerializeString(glob, DocType, "webm"); //Doc Type
  Ebml_SerializeUnsigned(glob, DocTypeVersion, 2); //Doc Type Version
  Ebml_SerializeUnsigned(glob, DocTypeReadVersion, 2); //Doc Type Read Version
  Ebml_EndSubElement(glob, &start);
}

void writeSimpleBlock(EbmlGlobal *glob, unsigned char trackNumber, short timeCode,
                      int isKeyframe, int invisible, unsigned char lacingFlag, int discardable,
                      unsigned char *data, unsigned long dataLength)
{
  Ebml_WriteID(glob, SimpleBlock);
  unsigned long blockLength = 4 + dataLength;
  blockLength |= 0x10000000; //TODO check length < 0x0FFFFFFFF
  Ebml_Serialize(glob, &blockLength, 4);
  trackNumber |= 0x80;  //TODO check track nubmer < 128
  Ebml_Write(glob, &trackNumber, 1);
  //Ebml_WriteSigned16(glob, timeCode,2); //this is 3 bytes
  Ebml_Serialize(glob, &timeCode, 2);
  unsigned char flags = 0x00 | (isKeyframe ? 0x80 : 0x00) |
                (invisible ?0x08 :0x00) | (lacingFlag << 1) | discardable;
  Ebml_Write(glob, &flags, 1);
  Ebml_Write(glob, data, dataLength);
}

static UInt64 generateTrackID(unsigned int trackNumber)
{
  UInt64 t = time(NULL) * trackNumber;
  UInt64 r = rand();
  r = r << 32;
  r +=  rand();
  UInt64 rval = t ^ r;
  return rval;
}

void writeVideoTrack(EbmlGlobal *glob, unsigned int trackNumber, int flagLacing,
                     char *codecId, unsigned int pixelWidth, unsigned int pixelHeight,
                     double frameRate)
{
  EbmlLoc start;
  Ebml_StartSubElement(glob, &start, TrackEntry);
  Ebml_SerializeUnsigned(glob, TrackNumber, trackNumber);
  UInt64 trackID = generateTrackID(trackNumber);
  Ebml_SerializeUnsigned(glob, TrackUID, trackID);
  Ebml_SerializeString(glob, CodecName, "VP8");  //TODO shouldn't be fixed
  
  Ebml_SerializeUnsigned(glob, TrackType, 1); //video is always 1
  Ebml_SerializeString(glob, CodecID, codecId);
  {
    EbmlLoc videoStart;
    Ebml_StartSubElement(glob, &videoStart, Video);
    Ebml_SerializeUnsigned(glob, PixelWidth, pixelWidth);
    Ebml_SerializeUnsigned(glob, PixelHeight, pixelHeight);
    Ebml_SerializeFloat(glob, FrameRate, frameRate);
    Ebml_EndSubElement(glob, &videoStart); //Video
  }
  Ebml_EndSubElement(glob, &start); //Track Entry
}
void writeAudioTrack(EbmlGlobal *glob, unsigned int trackNumber, int flagLacing,
                     char *codecId, double samplingFrequency, unsigned int channels,
                     unsigned char *private, unsigned long privateSize)
{
  EbmlLoc start;
  Ebml_StartSubElement(glob, &start, TrackEntry);
  Ebml_SerializeUnsigned(glob, TrackNumber, trackNumber);
  UInt64 trackID = generateTrackID(trackNumber);
  Ebml_SerializeUnsigned(glob, TrackUID, trackID);
  Ebml_SerializeUnsigned(glob, TrackType, 2); //audio is always 2
  Ebml_SerializeString(glob, CodecID, codecId);
  Ebml_SerializeData(glob, CodecPrivate, private, privateSize);
  
  Ebml_SerializeString(glob, CodecName, "VORBIS");  //fixed for now
  {
    EbmlLoc AudioStart;
    Ebml_StartSubElement(glob, &AudioStart, Audio);
    Ebml_SerializeFloat(glob, SamplingFrequency, samplingFrequency);
    Ebml_SerializeUnsigned(glob, Channels, channels);
    Ebml_EndSubElement(glob, &AudioStart);
  }
  Ebml_EndSubElement(glob, &start);
}
void writeSegmentInformation(EbmlGlobal *ebml, EbmlLoc* startInfo, unsigned long timeCodeScale, double duration)
{
  Ebml_StartSubElement(ebml, startInfo, Info);
  Ebml_SerializeUnsigned(ebml, TimecodeScale, timeCodeScale);
  Ebml_SerializeFloat(ebml, Segment_Duration, duration * 1000.0); //Currently fixed to using milliseconds
  Ebml_SerializeString(ebml, 0x4D80, "QTmuxingAppLibWebM-0.0.1");
  Ebml_SerializeString(ebml, 0x5741, "QTwritingAppLibWebM-0.0.1");
  Ebml_EndSubElement(ebml, startInfo);
}

