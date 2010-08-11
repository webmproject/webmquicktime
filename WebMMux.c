// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



#include <QuickTime/QuickTime.h>
#include "WebMExportStructs.h"
//#include "debug.h"

#include "EbmlIDs.h"
#include "log.h"
#include "WebMAudioStream.h"
#include "WebMMux.h"

#define kVorbisPrivateMaxSize  4000
#define kSInt16Max 32768

static ComponentResult _updateProgressBar(WebMExportGlobalsPtr globals, double percent);

static UInt64 secondsToTimeCode(WebMExportGlobalsPtr globals, double timeInSeconds)
{
    UInt64 rval = 0;

    if (globals->webmTimeCodeScale == 0)
    {
        dbg_printf("[webm] ERROR= division by 0 globals->webmTimeCodeScale\n");
        return 0;
    }

    rval = timeInSeconds * 1000000000 / globals->webmTimeCodeScale;
    return rval;
}

static double getMaxDuration(WebMExportGlobalsPtr globals)
{
    int i;
    double duration = 0.0;
    double dtmp = 0.0;
    TimeRecord durationTimeRec;

    // loop over all the data sources and find the max duration
    for (i = 0; i < globals->streamCount; i++)
    {
        GenericStream *gs = &(*globals->streams)[i];
        StreamSource *source;

        if (gs->trackType == VideoMediaType)
            source = &gs->stream.vid.source;
        else
            source = &gs->stream.aud.source;

        // get the track duration if it is available
        if (InvokeMovieExportGetPropertyUPP(source->refCon, source->trackID,
                                            movieExportDuration,
                                            &durationTimeRec,
                                            source->propertyProc) == noErr)
        {
            dtmp = (double) durationTimeRec.value.lo / (double) durationTimeRec.scale;
            dbg_printf("[webm] track duration # %d = %f\n", i, dtmp);

            if (duration < dtmp)
                duration = dtmp;
        }
    }

    return duration;
}

static ComponentResult _writeMetaData(WebMExportGlobalsPtr globals, EbmlGlobal *ebml,
                                      EbmlLoc *startSegment, double duration)
{
    ComponentResult err = noErr;
    int i;
    dbg_printf("[WebM]) Write segment information\n");
    writeSegmentInformation(ebml, globals->webmTimeCodeScale, duration * 1000000000.0);  //TODO timecodeScale currently hardcoded to millisecondes
    {
        EbmlLoc trackStart;
        Ebml_StartSubElement(ebml, &trackStart, Tracks);

        // loop over all the data sources and find the max duration
        for (i = 0; i < globals->streamCount; i++)
        {
            ComponentResult gErr = noErr;
            GenericStream *gs = &(*globals->streams)[i];
            dbg_printf("[WebM] Write track %d\n", i);

            if (gs->trackType == VideoMediaType)
            {
                VideoStreamPtr vs = &gs->stream.vid;
                double fps = FixedToFloat(globals->movie_fps);
                err = InvokeMovieExportGetDataUPP(vs->source.refCon, &vs->source.params,
                                                  vs->source.dataProc);
                ImageDescription *id = *(ImageDescriptionHandle) vs->source.params.desc;

                dbg_printf("[webM] write vid track #%d : %dx%d  %f fps\n",
                           vs->source.trackID, id->width, id->height, fps);
                writeVideoTrack(ebml, vs->source.trackID,
                                0, /*flag lacing*/
                                "V_VP8", id->width, id->height, fps);
            }
            else if (gs->trackType == SoundMediaType)
            {
                AudioStreamPtr as = &gs->stream.aud;
                unsigned int trackNumber;
                double sampleRate = 0;
                unsigned int channels = 0;

                if (as->vorbisComponentInstance == NULL)
                {

                    //Here I am setting the input properties for this component
                    err = initVorbisComponent(globals, as);

                    if (err) return err;

                    sampleRate = as->asbd.mSampleRate;
                    channels = as->asbd.mChannelsPerFrame;
                }

                UInt8 *privateData = NULL;
                UInt32 privateDataSize = 0;
                write_vorbisPrivateData(as, &privateData, &privateDataSize);
                dbg_printf("[WebM] Writing audio track %d with %d bytes private data, %d channels, %d sampleRate\n",
                           as->source.trackID, privateDataSize, channels, sampleRate);
                writeAudioTrack(ebml, as->source.trackID, 0 /*no lacing*/, "A_VORBIS" /*fixed for now*/,
                                sampleRate, channels, privateData, privateDataSize);
                dbg_printf("[WebM] finished audio write \n");

                if (privateData != NULL)
                    free(privateData);
            }
        }

        Ebml_EndSubElement(ebml, &trackStart);
    }
    dbg_printf("[webM] mux metadata = %d\n", err);
    return err;
}


static ComponentResult _updateProgressBar(WebMExportGlobalsPtr globals, double percent)
{
    ComponentResult err = noErr;

    if (globals->progressOpen == false)
    {
        InvokeMovieProgressUPP(NULL, movieProgressOpen,
                               progressOpExportMovie, 0,
                               globals->progressRefCon,
                               globals->progressProc);
        globals->progressOpen = true;
    }

    Fixed percentDone = FloatToFixed(percent);

    if (globals->progressProc)
    {

        if (percentDone > 0x010000)
            percentDone = 0x010000;

        err = InvokeMovieProgressUPP(NULL, movieProgressUpdatePercent,
                                     progressOpExportMovie, percentDone,
                                     globals->progressRefCon,
                                     globals->progressProc);
    }

    if (percentDone == 100.0 && globals->progressOpen)
    {
        InvokeMovieProgressUPP(NULL, movieProgressClose,
                               progressOpExportMovie, 0x010000,
                               globals->progressRefCon,
                               globals->progressProc);
        globals->progressOpen == false;
    }

    return err;
}

static void _writeCues(WebMExportGlobalsPtr globals, EbmlGlobal *ebml)
{
    EbmlLoc cuesHead;
    dbg_printf("[webm]_writeCues %d \n", globals->cueCount);
    Ebml_StartSubElement(ebml, &cuesHead, Cues);
    int i = 0;

    for (i = 0; i < globals->cueCount; i ++)
    {
        EbmlLoc cueHead;
        WebMCuePoint *cue = &(*globals->cueHandle)[globals->cueCount-1];
        unsigned long long cueLoc = *(SInt64 *)&cue->loc;
        dbg_printf("[WebM] Writing Cue track %d time %ld loc %lld\n",
                   cue->track, cue->timeVal, cueLoc);
        Ebml_StartSubElement(ebml, &cueHead, CuePoint);
        Ebml_SerializeUnsigned(ebml, CueTime, cue->timeVal);

        EbmlLoc trackLoc;
        Ebml_StartSubElement(ebml, &trackLoc, CueTrackPositions);
        //TODO this is wrong, get the conversion right
        Ebml_SerializeUnsigned(ebml, CueTrack, cue->track);
        Ebml_SerializeUnsigned64(ebml, CueClusterPosition, cueLoc);
        Ebml_SerializeUnsigned(ebml, CueBlockNumber, 1);
        Ebml_EndSubElement(ebml, &trackLoc);

        Ebml_EndSubElement(ebml, &cueHead);
    }

    Ebml_EndSubElement(ebml, &cuesHead);
}

void _addCue(WebMExportGlobalsPtr globals, UInt64 dataLoc, unsigned long time, unsigned int track)
{
    dbg_printf("[webm] _addCue time %ld loc %llu\n", time, dataLoc);
    globals->cueCount ++;

    if (globals->cueHandle)
        SetHandleSize((Handle) globals->cueHandle, sizeof(CuePoint) * globals->cueCount);
    else
        globals->cueHandle = (WebMCuePoint **) NewHandleClear(sizeof(WebMCuePoint));

    WebMCuePoint *newCue = &(*globals->cueHandle)[globals->cueCount-1];
    newCue->loc = dataLoc;
    newCue->timeVal = time;
    newCue->track = track;
}
static ComponentResult _compressVideo(WebMExportGlobalsPtr globals, VideoStreamPtr vs)
{
    ComponentResult err = noErr;

    if (vs->source.bQdFrame || vs->source.eos)
        return err; //paranoid check

    StreamSource *source =  &vs->source;
    double fps = FixedToFloat(globals->movie_fps);

    dbg_printf("[webM] call Compress Next frame %d at %f fps \n", vs->currentFrame, fps);
    // get next frame as vp8 frame
    err = compressNextFrame(globals, vs);

    if (err != noErr)
    {
        dbg_printf("[webM] compressNextFrame error %d\n", err);
    }

    if (fps != 0)
        source->time = (SInt32)((vs->currentFrame * 1.0) / fps * source->timeScale); //TODO  -- I am assuming that each frame has a similar fps
    else
        source->time += source->params.durationPerSample * source->timeScale
                        / source->params.sourceTimeScale;  //TODO might get clipping here

    if (!vs->source.eos)
        vs->source.bQdFrame = true;

    return err;

}

static void _startNewCluster(WebMExportGlobalsPtr globals, EbmlGlobal *ebml, unsigned long newTime)
{
    globals->clusterTime = newTime;
    Ebml_EndSubElement(ebml, &globals->clusterStart);
    Ebml_StartSubElement(ebml, &globals->clusterStart, Cluster);
    Ebml_SerializeUnsigned(ebml, Timecode, globals->clusterTime);
}

static ComponentResult _writeVideo(WebMExportGlobalsPtr globals, VideoStreamPtr vs, EbmlGlobal *ebml,
                                   SInt64 dataLoc)
{
    ComponentResult err = noErr;
    unsigned long lastTime = getTimeAsSeconds(&vs->source) * 1000;
    int isKeyFrame = vs->frame_type == kICMFrameType_I;
    dbg_printf("[webM] video write simple block track %d keyframe %d frame #%ld time %d data size %ld\n",
               vs->source.trackID, isKeyFrame,
               vs->currentFrame, lastTime, vs->outBuf.size);
    unsigned long relativeTime = lastTime - globals->clusterTime;

    if (isKeyFrame && vs->currentFrame != 0  || relativeTime > kSInt16Max / 2)
    {
        _startNewCluster(globals, ebml, lastTime);
        UInt64 tmpU = dataLoc;  ///should be able to use ebml->offset, but the compiler is messing this up somehow... don't rewrite this workaround
        _addCue(globals, tmpU, globals->clusterTime, vs->source.trackID);
        relativeTime =  0;
    }

    writeSimpleBlock(ebml, vs->source.trackID, (short)relativeTime,
                     isKeyFrame, 0 /*unsigned char lacingFlag*/, 0/*int discardable*/,
                     vs->outBuf.data, vs->outBuf.size);
    vs->source.bQdFrame = false;
    vs->currentFrame += 1;  //only advancing after the frame is written
    return err;
}

static ComponentResult _compressAudio(AudioStreamPtr as)
{
    ComponentResult err = noErr;

    if (as->source.bQdFrame)
        return err; //paranoid check

    err = compressAudio(as);

    if (err) return err;

    double timeSeconds = (1.0 * as->currentEncodedFrames) / (1.0 * as->asbd.mSampleRate);
    as->source.time = (SInt32)(timeSeconds * as->source.timeScale);
    dbg_printf("[webm] _compressAudio new audio time %f  %s\n", getTimeAsSeconds(&as->source), as->source.eos ? "eos" : "");

    if (!as->source.eos)
        as->source.bQdFrame = true;

    return err;
}

static ComponentResult _writeAudio(WebMExportGlobalsPtr globals, AudioStreamPtr as, EbmlGlobal *ebml)
{
    ComponentResult err = noErr;
    unsigned long lastTime = getTimeAsSeconds(&as->source) * 1000;
    unsigned long relativeTime = lastTime - globals->clusterTime;
    dbg_printf("[WebM] writing %d size audio packet with relative time %d, finishing time %f\n",
               as->outBuf.offset, relativeTime, getTimeAsSeconds(&as->source));

    if (relativeTime > kSInt16Max / 2)
    {
        _startNewCluster(globals, ebml, lastTime);
        relativeTime =  0;
    }

    writeSimpleBlock(ebml, as->source.trackID, (short)relativeTime,
                     1 /*audio always key*/, 0 /*unsigned char lacingFlag*/, 0/*int discardable*/,
                     as->outBuf.data, as->outBuf.offset);

    as->source.bQdFrame = false;
    return err;
}

ComponentResult muxStreams(WebMExportGlobalsPtr globals, DataHandler data_h)
{
    ComponentResult err = noErr;
    double duration = getMaxDuration(globals);
    dbg_printf("[WebM-%08lx] :: muxStreams( duration %d)\n", (UInt32) globals, duration);

    UInt32 iStream;
    Boolean allStreamsDone = false;

    //initialize my ebml writing structure
    EbmlGlobal ebml;
    ebml.data_h = data_h;
    ebml.offset.hi = 0;
    ebml.offset.lo = 0;

    EbmlLoc startSegment;
    globals->progressOpen = false;
	
	writeHeader(&ebml);
    Ebml_StartSubElement(&ebml, &startSegment, Segment);
	SInt64 firstL1Offset = *(SInt64*) &ebml.offset;  //The first level 1 element is the offset needed for cuepoints according to Matroska's specs

    _writeMetaData(globals, &ebml, &startSegment, duration);

    globals->clusterTime = 0 ;
    Ebml_StartSubElement(&ebml, &globals->clusterStart, Cluster);
    Ebml_SerializeUnsigned(&ebml, Timecode, globals->clusterTime);
    Boolean bExportVideo = globals->bMovieHasVideo && globals->bExportVideo;
    Boolean bExportAudio = globals->bMovieHasAudio && globals->bExportAudio;

    HLock((Handle)globals->streams);
    err = _updateProgressBar(globals, 0.0);

    double minTime = 1.e30; //very large number
    GenericStream *minTimeStream;

    while (!allStreamsDone /*&& lastTime < duration*/)
    {
        minTime = 1.e30;
        minTimeStream = NULL;
        allStreamsDone = true;
        SInt64 loc = *(SInt64 *)& ebml.offset;

        dbg_printf("[WebM]          ebml.offset  %lld\n", loc);

        //find the stream with the earliest time
        for (iStream = 0; iStream < globals->streamCount; iStream++)
        {
            GenericStream *gs = &(*globals->streams)[iStream];
            StreamSource *source;

            if (gs->trackType == VideoMediaType)
            {
                source =  &gs->stream.vid.source;

                if (!source->bQdFrame && globals->bExportVideo)
                    err = _compressVideo(globals, &gs->stream.vid);
            }

            if (gs->trackType == SoundMediaType)
            {
                source = &gs->stream.aud.source;

                if (!source->bQdFrame && globals->bExportAudio)
                    err = _compressAudio(&gs->stream.aud);
            }

            if (err)
            {
                dbg_printf("[webm] _compress error = %d\n", err);
                goto bail;
            }

            if (getTimeAsSeconds(source) < minTime && source->bQdFrame && !err)
            {
                minTime = getTimeAsSeconds(source);
                minTimeStream = gs;
                allStreamsDone = false;
            }
        }  //end for loop

        //write the stream with the earliest time
        if (minTimeStream == NULL)
            break;

        dbg_printf("[Webm] Stream with smallest time %f  %s\n",
                   minTime, minTimeStream->trackType == VideoMediaType ? "video" : "audio");

        if (minTimeStream->trackType == VideoMediaType)
        {
            VideoStreamPtr vs = &minTimeStream->stream.vid;
            _writeVideo(globals, vs, &ebml, loc - firstL1Offset);

        }  //end if VideoMediaType
        else if (minTimeStream->trackType == SoundMediaType)
        {
            AudioStreamPtr as = &minTimeStream->stream.aud;
            _writeAudio(globals, as, &ebml);
        } //end SoundMediaType

        Ebml_EndSubElement(&ebml, &globals->clusterStart);   //this writes cluster size multiple times, but works

        if (duration != 0.0)  //if duration is 0, can't show anything
            _updateProgressBar(globals, minTime / duration);
    }

    dbg_printf("[webm] done writing streams\n");
    _writeCues(globals, &ebml);
    Ebml_EndSubElement(&ebml, &startSegment);

    HUnlock((Handle) globals->streams);

    _updateProgressBar(globals, 100.0);
bail:
    dbg_printf("[WebM] <   [%08lx] :: muxStreams() = %ld\n", (UInt32) globals, err);
    return err;
}
