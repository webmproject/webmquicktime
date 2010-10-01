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
            source = &gs->vid.source;
        else
            source = &gs->aud.source;

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


static ComponentResult _writeTracks(WebMExportGlobalsPtr globals, EbmlGlobal *ebml, EbmlLoc* trackStart)
{
    ComponentResult err = noErr;
    int i;
    {
        Ebml_StartSubElement(ebml, trackStart, Tracks);

        // Write tracks
        for (i = 0; i < globals->streamCount; i++)
        {
            ComponentResult gErr = noErr;
            GenericStream *gs = &(*globals->streams)[i];
            dbg_printf("[WebM] Write track %d\n", i);

            if (gs->trackType == VideoMediaType)
            {
                VideoStreamPtr vs = &gs->vid;
                double fps = globals->framerate;
                if (fps == 0)
                {
                    //framerate estime should be replaced with more accurate
                    fps = vs->source.timeScale / 100.0;
                }
                //TODO this seems wrong, it seems like this is the input rather than the output
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
                AudioStreamPtr as = &gs->aud;
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

        Ebml_EndSubElement(ebml, trackStart);
    }
    dbg_printf("[webM] exit write trakcs = %d\n", err);
    return err;
}


static ComponentResult _updateProgressBar(WebMExportGlobalsPtr globals, double percent)
{
    ComponentResult err = noErr;
    //todo -- progress bar might need to be shut off in some cases
    if (globals->progressOpen == false)
    {
        err = InvokeMovieProgressUPP(NULL, movieProgressOpen,
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
        err = InvokeMovieProgressUPP(NULL, movieProgressClose,
                               progressOpExportMovie, 0x010000,
                               globals->progressRefCon,
                               globals->progressProc);
        globals->progressOpen == false;
    }

    return err;
}

static void _writeSeekElement(EbmlGlobal* ebml, unsigned long binaryId, EbmlLoc* Loc, UInt64 firstL1)
{
    UInt64 offset = *(SInt64*)&Loc->offset;
    offset = offset - firstL1 - 4; //constant 4(the length of the binary id)
    dbg_printf("[webm] Writing Element %lx at offset %lld\n", binaryId, offset);

    EbmlLoc start;
    Ebml_StartSubElement(ebml, &start, Seek);
    Ebml_SerializeBinary(ebml, SeekID, binaryId);
    Ebml_SerializeUnsigned64(ebml, SeekPosition, offset);
    Ebml_EndSubElement(ebml, &start);
}

static void _writeMetaSeekInformation(EbmlGlobal *ebml, EbmlLoc*  trackLoc, EbmlLoc*  cueLoc,  
                                      EbmlLoc*  segmentInformation, EbmlLoc* seekInfoLoc, SInt64 sFirstL1, Boolean firstWrite)
{
    EbmlLoc globLoc;
    UInt64 firstL1 = sFirstL1;
    //the first write is basicly space filler because where these elements are is unknown
    if (firstWrite)
    {
        Ebml_StartSubElement(ebml, seekInfoLoc, SeekHead);
    }
    else 
    {
        Ebml_GetEbmlLoc(ebml, &globLoc);    
        //Adding 8 which is the bytes that tell the size of the subElement
        SInt64 eightSI = 8;
        wide eight = *(wide*)&eightSI;
        WideAdd(&seekInfoLoc->offset, &eight);
        Ebml_SetEbmlLoc(ebml, seekInfoLoc);
    }
    SInt64 seekLoc = *(SInt64*)&seekInfoLoc->offset; 
    dbg_printf("[webm] Writing Seek Info to %lld\n", seekLoc);
    
    _writeSeekElement(ebml, Tracks, trackLoc, firstL1);
    _writeSeekElement(ebml, Cues, cueLoc, firstL1);
    _writeSeekElement(ebml, Info, segmentInformation, firstL1);
    
    if (firstWrite)
        Ebml_EndSubElement(ebml, seekInfoLoc);
    else
        Ebml_SetEbmlLoc(ebml, &globLoc);
}

static void _writeCues(WebMExportGlobalsPtr globals, EbmlGlobal *ebml, EbmlLoc *cuesLoc)
{
    dbg_printf("[webm]_writeCues %d \n", globals->cueCount);
    HLock(globals->cueHandle);
    Ebml_StartSubElement(ebml, cuesLoc, Cues);
    int i = 0;

    for (i = 0; i < globals->cueCount; i ++)
    {
        EbmlLoc cueHead;
        WebMCuePoint *cue = (WebMCuePoint*)(*globals->cueHandle + i * sizeof(WebMCuePoint));
        dbg_printf("[WebM] Writing Cue track %d time %ld loc %lld\n",
                   cue->track, cue->timeVal, cue->loc);
        Ebml_StartSubElement(ebml, &cueHead, CuePoint);
        Ebml_SerializeUnsigned(ebml, CueTime, cue->timeVal);

        EbmlLoc trackLoc;
        Ebml_StartSubElement(ebml, &trackLoc, CueTrackPositions);
        //TODO verify trackLoc
        Ebml_SerializeUnsigned(ebml, CueTrack, cue->track);
        Ebml_SerializeUnsigned64(ebml, CueClusterPosition, cue->loc);
        Ebml_SerializeUnsigned(ebml, CueBlockNumber, cue->blockNumber);
        Ebml_EndSubElement(ebml, &trackLoc);

        Ebml_EndSubElement(ebml, &cueHead);
    }

    Ebml_EndSubElement(ebml, cuesLoc);
    HUnlock((Handle)globals->cueHandle);
}

void _addCue(WebMExportGlobalsPtr globals, UInt64 dataLoc, unsigned long time, 
             unsigned int track, unsigned int blockNum)
{
    dbg_printf("[webm] _addCue %d time %ld loc %llu track %d blockNum %d\n",
               globals->cueCount, time, dataLoc, track, blockNum);
    globals->cueCount ++;
    long handleSize = sizeof(WebMCuePoint) * globals->cueCount;

    if (globals->cueHandle)
    {
        HUnlock(globals->cueHandle);  //important to unlock before moving
        SetHandleSize(globals->cueHandle, handleSize);
        HLock(globals->cueHandle);
    }
    else
        globals->cueHandle = NewHandleClear(handleSize);

    WebMCuePoint *newCue = (WebMCuePoint*) (*globals->cueHandle + handleSize - sizeof(WebMCuePoint));
    newCue->loc = dataLoc;
    newCue->timeVal = time;
    newCue->track = track;
    newCue->blockNumber = blockNum;
    dbg_printf("[webm] _addCue exit %d time %ld loc %llu track %d blockNum %d\n",
               globals->cueCount, newCue->timeVal, newCue->loc, newCue->track, newCue->blockNumber);
    dbg_printf("[WebM] globals->cueHandle Size %d\n", GetHandleSize(globals->cueHandle));
}
static ComponentResult _compressVideo(WebMExportGlobalsPtr globals, VideoStreamPtr vs)
{
    ComponentResult err = noErr;

    if (vs->source.bQdFrame || vs->source.eos)
        return err; //paranoid check

    dbg_printf("[webM] call Compress Next frame %d\n", vs->currentFrame);
    // get next frame as vp8 frame
    err = compressNextFrame(globals, vs);

    if (err != noErr)
    {
        dbg_printf("[webM] compressNextFrame error %d\n", err);
    }

    if (!vs->source.eos)
        vs->source.bQdFrame = true;

    return err;
}

static void _startNewCluster(WebMExportGlobalsPtr globals, EbmlGlobal *ebml)
{
    dbg_printf("[webm] Starting new cluster at %ld\n", globals->clusterTime);
    if (globals->clusterTime != 0)  //case of: first cluster (don't end non-existant previous)
        Ebml_EndSubElement(ebml, &globals->clusterStart);
    
    Ebml_StartSubElement(ebml, &globals->clusterStart, Cluster);
    Ebml_SerializeUnsigned(ebml, Timecode, globals->clusterTime);
}
static void _advanceVideoTime(WebMExportGlobalsPtr globals, VideoStreamPtr vs)
{
    StreamSource *source =  &vs->source;
    //this now represents the next frame we want to encode
    double fps = globals->framerate;
    if (fps == 0)
        fps = source->params.sourceTimeScale * 1.0/ source->params.durationPerSample * 1.0; 
    vs->currentFrame += 1;  
    source->time = (SInt32)((vs->currentFrame * 1.0) / fps * source->timeScale); //TODO  -- I am assuming that each frame has a similar fps
    /*source->time += source->params.durationPerSample * source->timeScale
     / source->params.sourceTimeScale;  //TODO precision loss??*/
    
    source->blockTimeMs = getTimeAsSeconds(source) * 1000;
    dbg_printf("[WebM] Next frame calculated %f from %f fps, durationPerSample %ld * timeScale %d / sourceTimeScale %d to %d \n"
               , getTimeAsSeconds(source),fps,  source->params.durationPerSample ,source->timeScale
               , source->params.sourceTimeScale, source->time);
    
}

static ComponentResult _writeVideo(WebMExportGlobalsPtr globals, VideoStreamPtr vs, EbmlGlobal *ebml)
{
    ComponentResult err = noErr;
    StreamSource *source =  &vs->source;
    unsigned long lastTime = source->blockTimeMs;
    int isKeyFrame = vs->frame_type == kICMFrameType_I;
    dbg_printf("[webM] video write simple block track %d keyframe %d frame #%ld time %d data size %ld\n",
               source->trackID, isKeyFrame,
               vs->currentFrame, lastTime, vs->outBuf.size);
    unsigned long relativeTime = lastTime - globals->clusterTime;


    writeSimpleBlock(ebml, source->trackID, (short)relativeTime,
                     isKeyFrame, 0 /*unsigned char lacingFlag*/, 0/*int discardable*/,
                     vs->outBuf.data, vs->outBuf.size);
    vs->source.bQdFrame = false;
    _advanceVideoTime( globals,  vs);
    
    return err;
}

static ComponentResult _compressAudio(AudioStreamPtr as)
{
    ComponentResult err = noErr;

    if (as->source.bQdFrame)
        return err; //paranoid check

    err = compressAudio(as);

    if (err) return err;


    if (!as->source.eos)
        as->source.bQdFrame = true;

    return err;
}

static ComponentResult _writeAudio(WebMExportGlobalsPtr globals, AudioStreamPtr as, EbmlGlobal *ebml)
{
    ComponentResult err = noErr;
    unsigned long lastTime = as->source.blockTimeMs;
    unsigned long relativeTime = lastTime - globals->clusterTime;
    dbg_printf("[WebM] writing %d size audio packet with relative time %d, packet time %d input stream time %f\n",
               as->outBuf.offset, relativeTime, lastTime, getTimeAsSeconds(&as->source));

    writeSimpleBlock(ebml, as->source.trackID, (short)relativeTime,
                     1 /*audio always key*/, 0 /*unsigned char lacingFlag*/, 0/*int discardable*/,
                     as->outBuf.data, as->outBuf.offset);
    double timeSeconds = (1.0 * as->currentEncodedFrames) / (1.0 * as->asbd.mSampleRate);
    as->source.blockTimeMs = (SInt32)(timeSeconds * 1000);
    
    dbg_printf("[webm] _compressAudio new audio time %f %d %s\n",
               getTimeAsSeconds(&as->source), as->source.blockTimeMs, as->source.eos ? "eos" : "");
    
    as->source.bQdFrame = false;
    return err;
}

Boolean isTwoPass(WebMExportGlobalsPtr globals)
{
    //DO a first pass if needed
    ComponentInstance videoCI =NULL;
    Boolean bTwoPass = false;
    ComponentResult err = getVideoComponentInstace(globals, &videoCI);
    if(err) 
    {
        dbg_printf("[WebM] getVideoComponentInstace err = %d\n",err);
        goto bail;
    }
    if (globals->videoSettingsCustom == NULL)
    {
        globals->videoSettingsCustom = NewHandleClear(0);
        SetHandleSize(globals->videoSettingsCustom, 0);
    }
    err = SCGetInfo(videoCI, scCodecSettingsType, &globals->videoSettingsCustom);
    if(err) 
    {
        dbg_printf("[WebM] SCGetInfo err = %d\n",err);
        goto bail;
    }
    
    globals->currentPass = 1;
    if (GetHandleSize(globals->videoSettingsCustom) > 8)
    {
        bTwoPass = ((UInt32*)*(globals->videoSettingsCustom))[1] ==2;
        dbg_printf("[WebM] globals->videoSettingsCustom)[0] = %4.4s  twoPass =%d\n",
                   &((UInt32*) *(globals->videoSettingsCustom))[0], bTwoPass);
    }
    else 
    {
        dbg_printf("[WebM] getVideoComponentInstace handleSize = %d\n",GetHandleSize(globals->videoSettingsCustom) );
    }

    
    
bail:
    if (videoCI != NULL)
    {
        CloseComponent(videoCI);
        videoCI= NULL;
    }
    return bTwoPass;
}


ComponentResult muxStreams(WebMExportGlobalsPtr globals, DataHandler data_h)
{
    ComponentResult err = noErr;
    double duration = getMaxDuration(globals);
    dbg_printf("[WebM-%08lx] :: muxStreams( duration %f)\n", (UInt32) globals, duration);
    
    UInt32 iStream;
    Boolean allStreamsDone = false;

    //initialize my ebml writing structure
    EbmlGlobal ebml;
    ebml.data_h = data_h;
    ebml.offset.hi = 0;
    ebml.offset.lo = 0;

    EbmlLoc startSegment, trackLoc, cuesLoc, segmentInfoLoc, seekInfoLoc;
    globals->progressOpen = false;
	
	writeHeader(&ebml);    
    dbg_printf("[WebM]) Write segment information\n");
    Ebml_StartSubElement(&ebml, &startSegment, Segment);
	SInt64 firstL1Offset = *(SInt64*) &ebml.offset;  //The first level 1 element is the offset needed for cuepoints according to Matroska's specs
    _writeMetaSeekInformation(&ebml, &trackLoc, &cuesLoc, &segmentInfoLoc, &seekInfoLoc, firstL1Offset, true);

    writeSegmentInformation(&ebml, &segmentInfoLoc, globals->webmTimeCodeScale, duration);  

    _writeTracks(globals, &ebml, &trackLoc);    

    Boolean bExportVideo = globals->bMovieHasVideo && globals->bExportVideo;
    Boolean bExportAudio = globals->bMovieHasAudio && globals->bExportAudio;
    

    HLock((Handle)globals->streams);
    err = _updateProgressBar(globals, 0.0);
    if (err) goto bail;

    unsigned long minTimeMs = ULONG_MAX;
    GenericStream *minTimeStream;

    globals->clusterTime = 0;  //assuming 0 start time
    Boolean startNewCluster = true;  //cluster should start very first
    unsigned int blocksInCluster=1;  //this increments any time a block added
    SInt64 clusterOffset = *(SInt64 *)& ebml.offset;
    
    Boolean bTwoPass = isTwoPass(globals);
    dbg_printf("[WebM] Is Two Pass %d\n",bTwoPass);
    
    for (iStream = 0; iStream < globals->streamCount; iStream++)
    {
        GenericStream *gs = &(*globals->streams)[iStream];
        gs->vid.bTwoPass = bTwoPass;
    }
    //start first pass in a two pass
    if (bTwoPass)
    {
        dbg_printf("[WebM] Itterating first pass on all videos\n");    
        allStreamsDone = false;
        //in a first pass do only video
        while (!allStreamsDone)
        {
            allStreamsDone = true;
            for (iStream = 0; iStream < globals->streamCount; iStream++)
            {
                GenericStream *gs = &(*globals->streams)[iStream];
                StreamSource *source;
            
                if (gs->trackType == VideoMediaType)
                {
                    source =  &gs->vid.source;
                
                    if (!source->bQdFrame && globals->bExportVideo)
                    {
                        err = _compressVideo(globals, &gs->vid);
                        _advanceVideoTime(globals, &gs->vid);
                    }
                    if (source->bQdFrame)
                    {
                        allStreamsDone = false;
                        source->bQdFrame= false;
                    }
                }
                minTimeMs = source->blockTimeMs;
            }
            
            if (duration != 0.0)  //if duration is 0, can't show anything
            {
                double percentComplete = minTimeMs / 1000.0 / duration;
                /*if (bTwoPass)
                 percentComplete = 50.0 + percentComplete/2.0;*/
                err = _updateProgressBar(globals, percentComplete );
            }
        }
        dbg_printf("[WebM] Ending First Pass\n");    
        for (iStream = 0; iStream < globals->streamCount; iStream++)
        {
            GenericStream *gs = &(*globals->streams)[iStream];
            if (gs->trackType == VideoMediaType)
            {
                endPass(&gs->vid);
                //reset the stream to the start
                gs->vid.source.eos = false;
                gs->vid.source.bQdFrame = false;
                gs->vid.source.blockTimeMs = 0;
                gs->vid.source.time = 0;
            }
        }
        
        for (iStream = 0; iStream < globals->streamCount; iStream++)
        {
            GenericStream *gs = &(*globals->streams)[iStream];
            if (gs->trackType == VideoMediaType)
            {
                startPass(&gs->vid, 2);
            }
        }
    } //end if bTwoPass
    
    
    while (!allStreamsDone)
    {
        minTimeMs = ULONG_MAX;
        minTimeStream = NULL;
        allStreamsDone = true;
        

        //find the stream with the earliest time
        for (iStream = 0; iStream < globals->streamCount; iStream++)
        {
            GenericStream *gs = &(*globals->streams)[iStream];
            StreamSource *source;

            if (gs->trackType == VideoMediaType)
            {
                source =  &gs->vid.source;

                if (!source->bQdFrame && globals->bExportVideo)
                {
                    err = _compressVideo(globals, &gs->vid);
                    //I need to know if there's a video keyframe in the Queue
                    if (!startNewCluster)
                        startNewCluster = gs->vid.frame_type == kICMFrameType_I;
                }
            }

            if (gs->trackType == SoundMediaType)
            {
                source = &gs->aud.source;

                if (!source->bQdFrame && globals->bExportAudio)
                    err = _compressAudio(&gs->aud);
            }

            if (err)
            {
                dbg_printf("[webm] _compress error = %d\n", err);
                goto bail;
            }
			
            Boolean smallerTime = false;
            if (gs->trackType == VideoMediaType)
                smallerTime = source->blockTimeMs < minTimeMs;
            else if (gs->trackType == SoundMediaType)
                smallerTime = source->blockTimeMs <= minTimeMs; //similar time audio first (see webm specs)									
			
            if (smallerTime && source->bQdFrame && !err)
            {
                minTimeMs = source->blockTimeMs;
                minTimeStream = gs;
                allStreamsDone = false;
            }
        }  //end for loop

        
        //write the stream with the earliest time
        if (minTimeStream == NULL)
            break;
        
        
        dbg_printf("[Webm] Stream with smallest time %d(ms)  %s: start Cluster %d\n",
                   minTimeMs, minTimeStream->trackType == VideoMediaType ? "video" : "audio", startNewCluster);

        if (minTimeMs - globals->clusterTime > 32767)
            startNewCluster = true; //keep in mind the block time offset to the cluster is SInt16
        
        if (startNewCluster)
        {
            globals->clusterTime = minTimeMs;
            blocksInCluster =1;
            clusterOffset = *(SInt64 *)& ebml.offset;
            dbg_printf("[WebM] Start new cluster offset %lld time %ld\n", clusterOffset, minTimeMs);
            _startNewCluster(globals, &ebml);
            startNewCluster = false;
        }

        
        if (minTimeStream->trackType == VideoMediaType)
        {
            VideoStreamPtr vs = &minTimeStream->vid;
            if( vs->frame_type == kICMFrameType_I)
            {
                UInt64 tmpU = clusterOffset - firstL1Offset;  
                _addCue(globals, tmpU , vs->source.blockTimeMs, vs->source.trackID, blocksInCluster);
            }
            _writeVideo(globals, vs, &ebml);
            blocksInCluster ++;            

        }  //end if VideoMediaType
        else if (minTimeStream->trackType == SoundMediaType)
        {
            AudioStreamPtr as = &minTimeStream->aud;
            _writeAudio(globals, as, &ebml);
            blocksInCluster ++;
        } //end SoundMediaType

        Ebml_EndSubElement(&ebml, &globals->clusterStart);   //this writes cluster size multiple times, but works

        if (duration != 0.0)  //if duration is 0, can't show anything
        {
            double percentComplete = minTimeMs / 1000.0 / duration;
            /*if (bTwoPass)
                percentComplete = 50.0 + percentComplete/2.0;*/
            err = _updateProgressBar(globals, percentComplete );
        }
        if (err ) goto bail;
    }
    
    if (bTwoPass)
    {
        for (iStream = 0; iStream < globals->streamCount; iStream++)
        {
            GenericStream *gs = &(*globals->streams)[iStream];
            if (gs->trackType == VideoMediaType)
            {
                endPass(&gs->vid, 1);
            }
        }
    }

    dbg_printf("[webm] done writing streams\n");
    //cues written at the end
    _writeCues(globals, &ebml, &cuesLoc);
    Ebml_EndSubElement(&ebml, &startSegment);

        //here I am rewriting the metaSeekInformation
    _writeMetaSeekInformation(&ebml, &trackLoc, &cuesLoc, &segmentInfoLoc, &seekInfoLoc, firstL1Offset, false);

    HUnlock((Handle) globals->streams);

    err = _updateProgressBar(globals, 100.0);
bail:
    dbg_printf("[WebM] <   [%08lx] :: muxStreams() = %ld\n", (UInt32) globals, err);
    return err;
}
