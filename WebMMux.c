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
    source = &gs->source;

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
      dbg_printf("[WebM] Write track %d %s\n", i, gs->trackType == VideoMediaType ? "video" : "audio");

      if (gs->trackType == VideoMediaType)
      {
        double fps = globals->framerate;
        if (fps == 0)
        {
                    
          fps = gs->source.timeScale / 100.0;  //framerate estime should be replaced with more accurate
          globals->framerate = fps;
        }
        dbg_printf("[WebM] WriteTrack frameRate %f\n",fps);
        //TODO verify this is the output
        err = InvokeMovieExportGetDataUPP(gs->source.refCon, &gs->source.params,
                                          gs->source.dataProc);
        ImageDescription *id = *(ImageDescriptionHandle) gs->source.params.desc;

        dbg_printf("[webM] write vid track #%d : %dx%d  %f fps\n",
                   gs->source.trackID, id->width, id->height, fps);
        writeVideoTrack(ebml, gs->source.trackID,
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
          err = initVorbisComponent(globals, gs);

          if (err) return err;

          sampleRate = as->asbd.mSampleRate;
          channels = as->asbd.mChannelsPerFrame;
        }

        UInt8 *privateData = NULL;
        UInt32 privateDataSize = 0;

        write_vorbisPrivateData(gs, &privateData, &privateDataSize);
        dbg_printf("[WebM] Writing audio track %d with %d bytes private data, %d channels, %d sampleRate\n",
                   gs->source.trackID, privateDataSize, channels, sampleRate);
        writeAudioTrack(ebml, gs->source.trackID, 0 /*no lacing*/, "A_VORBIS" /*fixed for now*/,
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
             unsigned int track)
{
  dbg_printf("[webm] _addCue %d time %ld loc %llu track %d blockNum %d\n",
             globals->cueCount, time, dataLoc, track, globals->blocksInCluster);
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
  newCue->blockNumber = globals->blocksInCluster;
  dbg_printf("[webm] _addCue exit %d time %ld loc %llu track %d blockNum %d\n",
             globals->cueCount, newCue->timeVal, newCue->loc, newCue->track, newCue->blockNumber);
  dbg_printf("[WebM] globals->cueHandle Size %d\n", GetHandleSize(globals->cueHandle));
}

static void _startNewCluster(WebMExportGlobalsPtr globals, EbmlGlobal *ebml)
{
  dbg_printf("[webm] Starting new cluster at %ld\n", globals->clusterTime);
  if (globals->clusterTime != 0)  //case of: first cluster (don't end non-existant previous)
    Ebml_EndSubElement(ebml, &globals->clusterStart);

  Ebml_StartSubElement(ebml, &globals->clusterStart, Cluster);
  Ebml_SerializeUnsigned(ebml, Timecode, globals->clusterTime);
  globals->blocksInCluster =1;
}

void GetEncoderSettings(WebMExportGlobalsPtr globals, Boolean *bIsTwoPass, Boolean *bAltRefEnabled)
{
  //DO a first pass if needed
  ComponentInstance videoCI =NULL;
  *bIsTwoPass = false;
  *bAltRefEnabled = false;
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
    *bIsTwoPass = ((UInt32*)*(globals->videoSettingsCustom))[1] ==2;
    dbg_printf("[WebM] globals->videoSettingsCustom)[0] = %4.4s  twoPass =%d\n",
               &((UInt32*) *(globals->videoSettingsCustom))[0], *bIsTwoPass);
    *bAltRefEnabled = ((UInt32*)*(globals->videoSettingsCustom))[25] ==1;
    dbg_printf("[WebM] globals->videoSettingsCustom)[0] = %4.4s  Altref =%d\n",
               &((UInt32*) *(globals->videoSettingsCustom))[0], *bAltRefEnabled);
  }
  else
  {
    dbg_printf("[WebM] getVideoComponentInstace handleSize = %d\n",GetHandleSize(globals->videoSettingsCustom) );
  }
  
  //get framerate here if possible
  SCTemporalSettings ts;
  err= SCGetInfo(videoCI, scTemporalSettingsType, &ts);
  double fps = FixedToFloat(ts.frameRate);
  //I'm not sure if this if statement is needed here
  dbg_printf("[WebM] SCGetInfo temporal settings returning framerate of %lf, previous fps\n", fps, globals->framerate);
  if (fps != 0 && globals->framerate != 0)
  {
    globals->framerate = fps;
  }
bail:
  if (videoCI != NULL)
  {
    CloseComponent(videoCI);
    videoCI= NULL;
  }
}

ComponentResult _doFirstPass(WebMExportGlobalsPtr globals)
{
  ComponentResult err = noErr;
  if (!globals->bExportVideo)
    return err;

  UInt32 iStream;
  Boolean allStreamsDone = false;
  double duration = getMaxDuration(globals);
  UInt64 timeMs = ULONG_MAX;

  dbg_printf("[WebM] Itterating first pass on all videos\n");
  allStreamsDone = false;
  //in a first pass do only video
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->trackType == VideoMediaType)
    {
      gs->vid.bTwoPass = true;
    }
  }

  while (!allStreamsDone)
  {
    timeMs = 0;
    allStreamsDone = true;
    for (iStream = 0; iStream < globals->streamCount; iStream++)
    {
      GenericStream *gs = &(*globals->streams)[iStream];
      StreamSource *source;

      if (gs->trackType == VideoMediaType)
      {
        err = compressNextFrame(globals, gs);
        if (!gs->complete)
          allStreamsDone = false;
        timeMs = gs->source.time * 1000 / gs->source.timeScale;
      }
    }
    if (duration != 0.0)  //if duration is 0, can't show anything
    {
      double percentComplete = timeMs / 1000.0 / duration /2.0;
      err = _updateProgressBar(globals, percentComplete);
    }
  }
  dbg_printf("[WebM] Ending First Pass\n");
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->trackType == VideoMediaType)
    {
      endPass(gs);
      //reset the stream to the start
      gs->source.eos = false;
      gs->source.time = 0;
      gs->complete = false;
      gs->framesIn = 0;
      gs->framesOut = 0;

    }
  }

  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->trackType == VideoMediaType)
    {
      startPass(gs, 2);
    }
  }

  allStreamsDone = false;    //reset
  return err;
}

ComponentResult _compressEmptyStreams(WebMExportGlobalsPtr globals)
{
  ComponentResult err = noErr;
  UInt32 iStream;
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->trackType == VideoMediaType && globals->bExportVideo)
    {
      if (gs->frameQueue.size == 0)
        err = compressNextFrame(globals, gs);
    }
    if (gs->trackType == SoundMediaType && globals->bExportAudio)
    {
      if (gs->frameQueue.size == 0)
        err = compressAudio(gs);
    }
    if (err)
    {
      dbg_printf("[webm] compress error = %d\n", err);
      goto bail;
    }
  }
bail:
  return err;
}


Boolean _checkAllStreamsComplete(WebMExportGlobalsPtr globals)
{
  UInt32 iStream;
  Boolean allStreamsDone = true;
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (!gs->complete || gs->frameQueue.size > 0)
    {
      allStreamsDone = false;
    }
  }
  return allStreamsDone;
}

//If waiting for data, minTimeStream should be null
ComponentResult _getStreamWithMinTime(WebMExportGlobalsPtr globals, GenericStream **minTimeStream, UInt64* minTimeMs)
{
  ComponentResult err = noErr;
  UInt32 iStream;
  *minTimeMs = ULONG_MAX;
  *minTimeStream = NULL;

  //see if there's an empty queue
  //TODO : this seems as though it shouldn't happen
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->frameQueue.size == 0  && !gs->complete)
    {
      return noErr;
    }
  }

  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->frameQueue.size == 0 && gs->complete)
      continue;  //if the stream is complete just continue
    
    WebMBufferedFrame* frame = gs->frameQueue.queue[0];
    dbg_printf("[WebM] %lu Frame Time = %ld\n",iStream, frame->timeMs);

    Boolean bIsSmaller = false;
    //audio media takes priority if sizes are similar
    if(gs->trackType == VideoMediaType)
      bIsSmaller = *minTimeMs > frame->timeMs;
    else if (gs->trackType == SoundMediaType)
      bIsSmaller = *minTimeMs >= frame->timeMs;
    if (bIsSmaller)
    {
      *minTimeMs = frame->timeMs;
      *minTimeStream = gs;
    }
  }
  dbg_printf("[Webm] Stream with smallest time %d(ms) %s\n",
             *minTimeMs,  ((*minTimeStream)->trackType == VideoMediaType) ?"video":"audio");
bail:
  return err;
}

void _startClusterIfNeeded(WebMExportGlobalsPtr globals, EbmlGlobal *ebml, UInt32 minTimeMs)
{
  UInt32 iStream;
  if (minTimeMs - globals->clusterTime > 32767)
    globals->startNewCluster = true; //keep in mind the block time offset to the cluster is SInt16

  //see if there is a video key frame
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->frameQueue.size ==0 || gs->trackType != VideoMediaType)
      continue;
    WebMBufferedFrame* frame = gs->frameQueue.queue[0];
    if ((frame->frameType & KEY_FRAME != 0) && frame->timeMs != globals->clusterKeyFrameTime)
    {
      globals->clusterKeyFrameTime = frame->timeMs;
      globals->startNewCluster = true;
    }
  }

  //TODO make clusters star with video keyframes
  if (globals->startNewCluster)
  {
    globals->clusterTime = minTimeMs;
    globals->blocksInCluster =1;
    globals->clusterOffset = *(SInt64 *)& ebml->offset;
    dbg_printf("[WebM] Start new cluster offset %lld time %ld\n", globals->clusterOffset, minTimeMs);
    _startNewCluster(globals, ebml);
    globals->startNewCluster = false;
  }
}


ComponentResult _writeBlock(WebMExportGlobalsPtr globals, GenericStreamPtr gs, EbmlGlobal* ebml)
{
  ComponentResult err = noErr;
  WebMBufferedFrame *frame = gs->frameQueue.queue[0];

  int isKeyFrame = (frame->frameType & KEY_FRAME) != 0;
  int invisible = (frame->frameType & ALT_REF_FRAME) !=0;
  short relativeTime = frame->timeMs - globals->clusterTime;
  dbg_printf("[webM] write simple block track %d keyframe %d invisible %d frame #%llu time %llu data size %lu, Relative Time %d\n",
             gs->source.trackID, isKeyFrame, invisible,
             gs->framesOut, frame->timeMs, frame->size, relativeTime);

  //NOTE: right now alt ref frames are not marked invisible
  writeSimpleBlock(ebml, gs->source.trackID, relativeTime,
                   isKeyFrame, 0, 0 , 0,
                   frame->data, frame->size);
  dbg_printf("[webM] Queue Size %d", gs->frameQueue.size);
  popFrame(&gs->frameQueue);
  //TODO this if statement needs to come out later, audio video have different
  //framesOut meanings
  if(gs->trackType == VideoMediaType)
      gs->framesOut ++;

  return err;
}

static void _endSecondPass(WebMExportGlobalsPtr globals)
{
  UInt32 iStream;
  for (iStream = 0; iStream < globals->streamCount; iStream++)
  {
    GenericStream *gs = &(*globals->streams)[iStream];
    if (gs->trackType == VideoMediaType)
      endPass(gs);
  }
}



ComponentResult muxStreams(WebMExportGlobalsPtr globals, DataHandler data_h)
{
  ComponentResult err = noErr;
  UInt64 minTimeMs;
  double duration = getMaxDuration(globals);
  dbg_printf("[WebM-%08lx] :: muxStreams( duration %f)\n", (UInt32) globals, duration);
  WebMBufferedFrame * minFrame;


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

  GenericStream *minTimeStream;

  globals->clusterTime = 0;  //assuming 0 start time
  globals->startNewCluster = true;  //cluster should start very first
  globals->blocksInCluster =1;
  globals->clusterOffset = *(SInt64 *)& ebml.offset;
  globals->clusterKeyFrameTime = UINT_MAX;

  Boolean bTwoPass;
  GetEncoderSettings(globals, &bTwoPass, &globals->bAltRefEnabled);
  dbg_printf("[WebM] Is Two Pass %d\n",bTwoPass);
  //start first pass in a two pass
  if (bTwoPass)
    _doFirstPass(globals);


  while (!allStreamsDone)
  {
    allStreamsDone = false;

    err = _compressEmptyStreams(globals);
    if (err) goto bail;

    allStreamsDone = _checkAllStreamsComplete(globals);
    if (allStreamsDone)
      break;

    err = _getStreamWithMinTime(globals, &minTimeStream, &minTimeMs);
    if (err) goto bail;

    //if all frames that should be available find the earliest time:
    if (minTimeStream == NULL)  //some streams are waiting for compressed data
      continue;
    //write the stream with the earliest time
    _startClusterIfNeeded(globals, &ebml, minTimeMs);

    minFrame = minTimeStream->frameQueue.queue[0];

    if (minTimeStream->trackType == VideoMediaType && (minFrame->frameType & KEY_FRAME) != 0)
    {
        UInt64 tmpU = globals->clusterOffset - firstL1Offset;
        _addCue(globals, tmpU , minFrame->timeMs, minTimeStream->source.trackID);
    }  //end if VideoMediaType
    _writeBlock(globals, minTimeStream, &ebml);


    globals->blocksInCluster ++;

    Ebml_EndSubElement(&ebml, &globals->clusterStart);   //this writes cluster size multiple times, but works

    if (duration != 0.0)  //if duration is 0, can't show anything
    {
      double percentComplete = minTimeMs / 1000.0 / duration;
      if (bTwoPass)
        percentComplete = 0.5 + percentComplete/2.0;
      err = _updateProgressBar(globals, percentComplete );
    }
    if (err ) goto bail;
  }

  dbg_printf("[webm] done writing streams\n");
  if (bTwoPass)
    _endSecondPass(globals);

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
