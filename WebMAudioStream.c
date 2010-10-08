// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include <QuickTime/QuickTime.h>
#include <AudioUnit/AudioUnit.h>
#include "WebMExportStructs.h"

#include "WebMExportVersions.h"
#include "WebMAudioStream.h"
#include "Raw_debug.h"


static void _printSoundDesc(SoundDescriptionV2Ptr sd)
{
    if (sd == NULL)
    {
        dbg_printf("[WebM] SoundDesc NULL\n");
        return;
    }
    dbg_printf("[WebM] SoundDesc format '%4.4s', version %d, revlevel %d, vendor '%4.4s',"
               "[num channels %ld, bits per channel %ld, audio Samplereate %lf] "
               "[ LPCM Frames %ld, const Bytes %ld,  Format Flags%ld])\n",
               (char *) &sd->dataFormat, sd->version, sd->revlevel, (char *) &sd->vendor,
               sd->numAudioChannels, sd->constBitsPerChannel, sd->audioSampleRate,
               sd->constLPCMFramesPerAudioPacket, sd->constBytesPerAudioPacket, sd->formatSpecificFlags);
}

static void _printAudioStreamDesc(AudioStreamBasicDescription *desc)
{
    dbg_printf("[WebM] stream desc asbd = %f sampleRate per frame, %d Frames Per packet, %d bytes per frame\n"
               "     %d bytes per packet, %d Channels per frame\n",
               desc->mSampleRate, desc->mFramesPerPacket, desc->mBytesPerFrame,
               desc->mBytesPerPacket, desc->mChannelsPerFrame);
}

ComponentResult getInputBasicDescription(GenericStreamPtr as, AudioStreamBasicDescription *inFormat)
{
    dbg_printf("[webm] enter getInputBasicDescription\n" );
    ComponentResult err = noErr;

    initMovieGetParams(&as->source);

    dbg_printf("[webm] getInputBasicDescription InvokeMovieExportGetDataUPP\n" );
    dbg_printDataParams(&as->source);
    err = InvokeMovieExportGetDataUPP(as->source.refCon, &as->source.params, as->source.dataProc);

    if (err) goto bail;

    SoundDescriptionHandle sdh = NULL;
    dbg_printf("[webm] getInputBasicDescription QTSoundDescriptionConvert\n" );
    err = QTSoundDescriptionConvert(kQTSoundDescriptionKind_Movie_AnyVersion,
                                    (SoundDescriptionHandle) as->source.params.desc,
                                    kQTSoundDescriptionKind_Movie_Version2,
                                    &sdh);
    SoundDescriptionV2Ptr sd = (SoundDescriptionV2Ptr) * sdh;
    _printSoundDesc(sd);

    if (sd->dataFormat != kAudioFormatLinearPCM)
    {
        dbg_printf("[Webm] - compressed formats not supported\n");
        err = paramErr;
        goto bail;
    }

    inFormat->mSampleRate = sd->audioSampleRate;
    inFormat->mFormatID = sd->dataFormat;
    inFormat->mFormatFlags = sd->formatSpecificFlags;
    inFormat->mFramesPerPacket = sd->constLPCMFramesPerAudioPacket;
    inFormat->mBytesPerFrame = sd->constBytesPerAudioPacket;
    inFormat->mChannelsPerFrame = sd->numAudioChannels;
    inFormat->mBitsPerChannel = sd->constBitsPerChannel;
    inFormat->mReserved = 0;
    //calculated
    inFormat->mBytesPerPacket = inFormat->mBytesPerFrame *  inFormat->mFramesPerPacket;

bail:

    if (sdh != NULL)
        DisposeHandle((Handle) sdh);

    _printAudioStreamDesc(inFormat);
    return err;
}




ComponentResult initVorbisComponent(WebMExportGlobalsPtr globals, GenericStreamPtr as)
{
    dbg_printf("[WebM] enter initVorbisComponent\n");
    ComponentResult err = noErr;
    if (globals->audioSettingsAtom == NULL)
        getDefaultVorbisAtom(globals);

    //This chunk initializes the Component instance that will be used for decompression  : TODO put this in its own function
    err = OpenADefaultComponent(StandardCompressionType, StandardCompressionSubTypeAudio, &as->aud.vorbisComponentInstance);

    if (err) goto bail;

    AudioStreamBasicDescription *inFormat = NULL;
    inFormat = calloc(1, sizeof(AudioStreamBasicDescription));

    if (inFormat == NULL) goto bail;

    err = getInputBasicDescription(as, inFormat);

    if (err) goto bail;

    getInputBasicDescription(as, inFormat);


    err = SCSetSettingsFromAtomContainer(as->aud.vorbisComponentInstance, globals->audioSettingsAtom);

    if (err) goto bail;

    err = QTGetComponentProperty(as->aud.vorbisComponentInstance, kQTPropertyClass_SCAudio,
                                 kQTSCAudioPropertyID_BasicDescription,
                                 sizeof(AudioStreamBasicDescription), &as->aud.asbd, NULL);

    if (err) goto bail;

    err = QTSetComponentProperty(as->aud.vorbisComponentInstance,  kQTPropertyClass_SCAudio, kQTSCAudioPropertyID_InputBasicDescription,
                                 sizeof(AudioStreamBasicDescription), inFormat);
bail:

    if (inFormat != NULL)
        free(inFormat);

    dbg_printf("[WebM]initVorbisComponent return %d\n", err);
    return err;
}




static ComponentResult
_fillBuffer_callBack(ComponentInstance ci, UInt32 *ioNumberDataPackets, AudioBufferList *ioData,
                     AudioStreamPacketDescription **outDataPacketDescription, void *inRefCon)
{
    GenericStreamPtr as = (GenericStreamPtr) inRefCon;
    StreamSource *source = &as->source;
    MovieExportGetDataParams *params = &source->params;

    ComponentResult err = noErr;

    dbg_printf("[WebM] _fillBuffer_callBack(%ld), current samples = %d\n",
               *ioNumberDataPackets, params->actualSampleCount);

    const UInt32 packetsRequested = *ioNumberDataPackets;

    *ioNumberDataPackets = 0;

    if (source->eos)
        return err;

    if (params->actualSampleCount > *ioNumberDataPackets)
    {
        //we already have samples
        dbg_printDataParams(source);
        if (params->actualSampleCount > *ioNumberDataPackets)
        {
            //this error should not happen.
            dbg_printf("[webm] *******error: TODO Potential unconsumed samples\n");
        }
    }
    else if (params->actualSampleCount == 0)
    {
        initMovieGetParams(source);
        params->requestedSampleCount = packetsRequested;
        err = InvokeMovieExportGetDataUPP(source->refCon, params, source->dataProc);
        dbg_printDataParams(source);

        if (err == eofErr)
        {
            //source->eos = true;
            dbg_printf("[Webm] Audio stream complete eos\n");
            return err;
        }

        if (err) return err;

        dbg_printf("[Webm] Audio Time = %f\n", getTimeAsSeconds(source));
    }

    if (params->actualSampleCount == 0)
    {
        dbg_printf("[WebM] fillBufferCallBack no more samples\n");
        ioData->mBuffers[0].mDataByteSize = 0;
        ioData->mBuffers[0].mData = NULL;
        source->eos = true;
        dbg_printf("[Webm] audio stream complete - no samples \n");
    }
    else
    {

        dbg_printf("[WebM] fillBufferCallBack %d samples, %d dataSize\n",
                   params->actualSampleCount, params->dataSize);
        ioData->mBuffers[0].mDataByteSize = params->dataSize;
        ioData->mBuffers[0].mData = params->dataPtr;

        source->time += params->durationPerSample * params->actualSampleCount;
        *ioNumberDataPackets = params->actualSampleCount;
        params->actualSampleCount = 0;
    }
    if (as->framesIn ==0)
    {
        //dbg_printf("TODO REMOVE -- mDataByteSize = %ld, num Packets = %ld",
        //           ioData->mBuffers[0].mDataByteSize, *ioNumberDataPackets);
    }
    as->framesIn += *ioNumberDataPackets;

    return err;
}



static void _initAudioBufferList(GenericStreamPtr as, AudioBufferList **audioBufferList, UInt32 ioPackets)
{
    int i;

    UInt32 maxBytesPerPacket = 4096;

    if (as->aud.asbd.mBytesPerPacket)
    {
        maxBytesPerPacket = as->aud.asbd.mBytesPerPacket;
    }
    else
    {
        if (QTGetComponentProperty(as->aud.vorbisComponentInstance, kQTPropertyClass_SCAudio,
                                   kQTSCAudioPropertyID_MaximumOutputPacketSize,
                                   sizeof(maxBytesPerPacket), &maxBytesPerPacket, NULL) != noErr)
            dbg_printf("[Webm] Error getting max Bytes per packet\n");

    }

    UInt32 bufferListSize = offsetof(AudioBufferList, mBuffers[ioPackets]);

    dbg_printf("[WebM]Calling InitAudioBufferList size %d, each buffer being %d\n", bufferListSize, maxBytesPerPacket);

    *audioBufferList = (AudioBufferList *) malloc(bufferListSize);
    (*audioBufferList)->mNumberBuffers = ioPackets;
    UInt32 wantedSize = maxBytesPerPacket * ioPackets;

    
    
    if (as->aud.buf.data == NULL || as->aud.buf.size != wantedSize)
    {
        allocBuffer(&as->aud.buf, wantedSize);
    }

    for (i = 0; i < ioPackets; i++)
    {
        (*audioBufferList)->mBuffers[i].mNumberChannels = as->aud.asbd.mChannelsPerFrame;
        (*audioBufferList)->mBuffers[i].mDataByteSize = maxBytesPerPacket;
        (*audioBufferList)->mBuffers[i].mData = (void *)((unsigned char *)as->aud.buf.data + maxBytesPerPacket * i);
    }
}


ComponentResult compressAudio(GenericStreamPtr as)
{
    ComponentResult err = noErr;

    if (as->source.eos)
        return noErr;  //shouldn't be called here.

    UInt32 ioPackets = 1; //I want to get only one packet at a put it in a simple block
    AudioStreamPacketDescription *packetDesc = NULL;

    packetDesc = (AudioStreamPacketDescription *)calloc(ioPackets, sizeof(AudioStreamPacketDescription));

    AudioBufferList *audioBufferList = NULL;
    _initAudioBufferList(as, &audioBufferList, ioPackets);  //allocates memory
    dbg_printf("[WebM] call SCAudioFillBuffer(%x,%x,%x,%x,%x, %x)\n", as->aud.vorbisComponentInstance, _fillBuffer_callBack,
               (void *) as, &ioPackets,
               audioBufferList, packetDesc);

    err = SCAudioFillBuffer(as->aud.vorbisComponentInstance, _fillBuffer_callBack,
                            (void *) as, &ioPackets,
                            audioBufferList, packetDesc);
    dbg_printf("[WebM] exit SCAudioFillBuffer %d packets, err = %d\n", ioPackets, err);


    if (err == eofErr)
    {
        dbg_printf("[WebM] Total Frames in = %lld, Total Frames Out = %lld\n",
                   as->framesIn, as->framesOut);
        if (ioPackets == 0)
            as->source.eos = true;
        err= noErr;
    }
    
    if (err) goto bail;

    if (ioPackets > 0)
    {
        as->aud.buf.offset = 0;
        int i = 0;

        for (i = 0; i < ioPackets; i++)
        {
            dbg_printf("[WebM] packet is %ld bytes, %ld frames\n", packetDesc[i].mDataByteSize,  packetDesc[i].mVariableFramesInPacket);
            if (packetDesc[i].mVariableFramesInPacket ==0)
            {
                //as->currentEncodedFrames += 13230; //0 indicates fixed frames, TODO this number just works for now( it seems wrong)
                //I think I had this number here earlier to account for a bug, which is now fixed...
                
                
                as->framesOut += 3092; //0 indicates fixed frames, TODO this number just works for now( it seems wrong)
            }
            else
                as->framesOut += packetDesc[i].mVariableFramesInPacket;
            as->aud.buf.offset += packetDesc[i].mDataByteSize;
        }
    }

bail:

    if (audioBufferList != NULL)
        free(audioBufferList);

    if (packetDesc != NULL)
        free(packetDesc);



    return err;
}


//The first three packets of ogg vorbis data are taken from the magic cookie.

enum
{
    kCookieTypeVorbisHeader = 'vCtH',
    kCookieTypeVorbisComments = 'vCt#',
    kCookieTypeVorbisCodebooks = 'vCtC',
    kCookieTypeVorbisFirstPageNo = 'vCtN'
};

struct CookieAtomHeader
{
    long           size;
    long           type;
    unsigned char  data[1];
};
typedef struct CookieAtomHeader CookieAtomHeader;

static void _oggLacing(UInt8 **ptr, UInt32 size)
{
    long tmpLacing = size;

    while (tmpLacing >= 0)
    {
        if (tmpLacing >= 255)
            **ptr = 255;
        else
        {
            UInt8 ui8tmp = tmpLacing;
            **ptr = ui8tmp;
        }

        (*ptr) ++;
        tmpLacing -= 255;
    }
}

static void _dbg_printVorbisHeader(const UInt8 *ptr)
{
    UInt32 vorbisVersion = *(UInt32 *)(&ptr[7]);
    UInt8 audioChannels = *(UInt8 *)(&ptr[11]);
    UInt32 audioSampleRate = *(UInt32 *)(&ptr[12]);
    long bitrate_maximum = *(long *)(&ptr[16]);
    long bitrate_nominal = *(long *)(&ptr[20]);
    long bitrate_minimum = *(long *)(&ptr[24]);
    UInt8 blockSize0 = ptr[28] >> 4;
    UInt8 blockSize1 = ptr[28] & 0x0f;

    dbg_printf("Vorbis header reads vers %ld channels %d sampleRate %ld\n"
               "\t bitrates max %ld nominal %ld min %ld\n"
               "\t blockSize_0 %d, blockSize_1 %d\n",
               vorbisVersion, audioChannels, audioSampleRate,
               bitrate_maximum, bitrate_nominal, bitrate_minimum,
               blockSize0, blockSize1);

}

ComponentResult write_vorbisPrivateData(GenericStreamPtr as, UInt8 **buf, UInt32 *bufSize)
{
    ComponentResult err = noErr;
    void *magicCookie = NULL;
    UInt32 cookieSize = 0;
    dbg_printf("[WebM] Get Vorbis Private Data\n");

    err = QTGetComponentPropertyInfo(as->aud.vorbisComponentInstance,
                                     kQTPropertyClass_SCAudio,
                                     kQTSCAudioPropertyID_MagicCookie,
                                     NULL, &cookieSize, NULL);

    if (err) return err;

    dbg_printf("[WebM] Cookie Size %d\n", cookieSize);

    magicCookie = calloc(1, cookieSize);
    err = QTGetComponentProperty(as->aud.vorbisComponentInstance,
                                 kQTPropertyClass_SCAudio,
                                 kQTSCAudioPropertyID_MagicCookie,
                                 cookieSize, magicCookie, NULL);

    if (err) goto bail;

    UInt8 *ptrheader = (UInt8 *) magicCookie;
    UInt8 *cend = ptrheader + cookieSize;
    CookieAtomHeader *aheader = (CookieAtomHeader *) ptrheader;
    WebMBuffer header, header_vc, header_cb;
    header.size = header_vc.size = header_cb.size = 0;

    while (ptrheader < cend)
    {
        aheader = (CookieAtomHeader *) ptrheader;
        ptrheader += EndianU32_BtoN(aheader->size);

        if (ptrheader > cend || EndianU32_BtoN(aheader->size) <= 0)
            break;

        switch (EndianS32_BtoN(aheader->type))
        {
        case kCookieTypeVorbisHeader:
            header.size = EndianS32_BtoN(aheader->size) - 2 * sizeof(long);
            header.data = aheader->data;
            break;

        case kCookieTypeVorbisComments:
            header_vc.size = EndianS32_BtoN(aheader->size) - 2 * sizeof(long);
            header_vc.data = aheader->data;
            break;

        case kCookieTypeVorbisCodebooks:
            header_cb.size = EndianS32_BtoN(aheader->size) - 2 * sizeof(long);
            header_cb.data = aheader->data;
            break;

        default:
            break;
        }
    }

    if (header.size == 0 || header_vc.size == 0 || header_cb.size == 0)
    {
        err = paramErr;
        goto bail;
    }

    //1 + header1 /255 + header2 /255 + idheader.len +
    *bufSize = 1;  //the first byte which is always 0x02
    *bufSize += (header.size - 1) / 255 + 1; //the header size lacing
    *bufSize += (header_vc.size - 1) / 255 + 1; //the comment size lacing
    *bufSize += header.size + header_vc.size + header_cb.size; //the packets
    dbg_printf("[WebM]Packet headers  %d %d %d -- total buffer %d\n",
               header.size, header_vc.size , header_cb.size, *bufSize);
    *buf = malloc(*bufSize);
    UInt8 *ptr = *buf;

    *ptr = 0x02;
    ptr ++;
    //using ogg lacing write out the size of the first two packets
    _oggLacing(&ptr, header.size);
    _oggLacing(&ptr, header_vc.size);

    _dbg_printVorbisHeader(header.data);

    memcpy(ptr, header.data, header.size);
    ptr += header.size;
    memcpy(ptr, header_vc.data, header_vc.size);
    ptr += header_vc.size;
    memcpy(ptr, header_cb.data, header_cb.size);

bail:

    if (magicCookie != NULL)
    {
        free(magicCookie);
        magicCookie = NULL;
    }

    return err;
}

ComponentResult initAudioStream(GenericStreamPtr as)
{
    as->aud.vorbisComponentInstance = NULL;
    memset(&as->aud.asbd, 0, sizeof(AudioStreamBasicDescription));
    as->framesOut = 0;
    as->framesIn =0;
    as->aud.buf.size =0;
    as->aud.buf.offset=0;
    as->aud.buf.data = NULL;

    return noErr;
}
