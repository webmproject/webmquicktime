// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

//
// Quicktime Movie Import Commponent ('eat ') for the WebM format.
// See www.webmproject.org for more info.
//
// The MkvBufferedReaderQT class uses asynchronous io to read WebM data into a buffer.  The "libwebm" project's
// libmkvparser.a library is used to parse the WebM data (which is similar to Matroska).  This implements an "idling"
// importer, and for every call to the Idle() routine we import one WebM Cluster.  Audio and video samples
// are collected in STL containers and then added to QuickTime Media and Track objects.  When the resulting QuickTime
// movie is played, the VP8 video data is passed to the QuickTime Video Decompressor component ('imdc') to be decoded.
// Audio data is sent to a separate Vorbis decoder component.
// When the "WebM.component" is installed in /Library/QuickTime/ folder, it enables playback of the WebM format
// in QuickTime Player 7, Safari, and other QuickTime applications on the Mac.  The vorbis decoder, XiphQT.component,
// must also be installed for audio support.
//


#include "WebMImport.hpp"

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>
#include <vector>

#include "mkvparser.hpp"
#include "mkvreaderqt.hpp"

extern "C" {
#include "log.h"
}

#define USE_PARSE_HEADERS 1

typedef std::vector<SampleReferenceRecord> SampleRefVec;
typedef std::vector<long long> SampleTimeVec;


// WebM Import Component Globals structure
typedef struct {
  ComponentInstance self;
  IdleManager idleManager;
  mkvparser::Segment* webmSegment;
  const mkvparser::Cluster* webmCluster;
  const mkvparser::Tracks* webmTracks;
  long long segmentDuration;
  ::Track movieVideoTrack, movieAudioTrack;
  ::Media movieVideoMedia, movieAudioMedia;
  ::Movie movie;
  ImageDescriptionHandle vp8DescHand; //videoDescHand;
  SoundDescriptionHandle audioDescHand;
  long audioSampleRate;
  Handle dataRef;
  OSType dataRefType;
  long  dataHOffset;
  long  fileSize;
  ComponentInstance dataHandler;
  SampleRefVec videoSamples;
  SampleTimeVec videoTimes;
  SampleRefVec audioSamples;
  SampleTimeVec audioTimes;
  long videoCount;                        // total count of video blocks added to Media
  long audioCount;                        // total count of audio blocks added to Media
  long trackCount;                        // number of tracks added to Movie
  long loadState;                         // kMovieLoadStateLoading, ... kMovieLoadStateComplete
  long videoMaxLoaded;                    // total duration of video blocks already inserted into QT Track, expressed in video media's time scale.
  long audioMaxLoaded;                    // total duration of audio blocks already inserted into QT Track, expressed in audio media's time scale.
  // last time we added samples, (in ~60/s ticks)
  unsigned long addSamplesLast;
  // is the component importing in idling mode?
  Boolean usingIdles;
  MkvBufferedReaderQT* reader;            // reader object passed to parser
} WebMImportGlobalsRec, *WebMImportGlobals;

static const long long ns_per_sec = 1000000000;

// minimum number of seconds between playhead and the end of added
// data we should maintain
static const long kMinPlayheadDistance = 5;
// minimum interval for considering adding samples (in ~60/s ticks)
static const long kAddSamplesCheckTicks = 30;
// normal interval for adding samples (in ~60/s ticks)
static const long kAddSamplesNormalTicks = 90;

enum { VIDEO_TRACK = 1, AUDIO_TRACK = 2 };    // track types

#define MOVIEIMPORT_BASENAME() WebMImport
#define MOVIEIMPORT_GLOBALS() WebMImportGlobals storage

#define CALLCOMPONENT_BASENAME()  MOVIEIMPORT_BASENAME()
#define CALLCOMPONENT_GLOBALS() MOVIEIMPORT_GLOBALS()

#define COMPONENT_DISPATCH_FILE "WebMImportDispatch.h"
#define COMPONENT_UPP_SELECT_ROOT() MovieImport

extern "C" {
#include <CoreServices/Components.k.h>
#include <QuickTime/QuickTimeComponents.k.h>
#include <QuickTime/ImageCompression.k.h>   // for ComponentProperty selectors
#include <QuickTime/ComponentDispatchHelper.c>
}

#pragma mark-
extern "C" {
  pascal ComponentResult WebMImportOpen(WebMImportGlobals store, ComponentInstance self);
  //...
}

OSErr CreateVP8ImageDescription(long width, long height, ImageDescriptionHandle *descOut);
OSErr CreateAudioDescription(SoundDescriptionHandle *descOut, const mkvparser::AudioTrack* webmAudioTrack);
OSErr CreateCookieFromCodecPrivate(const mkvparser::AudioTrack* webmAudioTrack, Handle* cookie);
OSErr AddCluster(WebMImportGlobals store, const mkvparser::Cluster* webmCluster, const mkvparser::Tracks* webmTracks, long long lastTime_ns);
OSErr AddAudioBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns, const mkvparser::AudioTrack* webmAudioTrack);
OSErr FinishAddingAudioBlocks(WebMImportGlobals store, long long lastTime_ns);
OSErr AddVideoBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns, const mkvparser::VideoTrack* webmVideoTrack);
OSErr FinishAddingVideoBlocks(WebMImportGlobals store, long long lastTime_ns);
static Boolean ShouldAddSamples(WebMImportGlobals store);
void DumpWebMDebugInfo(mkvparser::EBMLHeader* ebmlHeader, const mkvparser::SegmentInfo* webmSegmentInfo, const mkvparser::Tracks* webmTracks);
void DumpWebMGlobals(WebMImportGlobals store);



#pragma mark -
//--------------------------------------------------------------------------------
// Component Open Request - Required
pascal ComponentResult WebMImportOpen(WebMImportGlobals store, ComponentInstance self)
{
  OSErr err;
  dbg_printf("[WebM Import]  >> [%08lx] :: Open()\n", (UInt32) store); //globals);

  store = (WebMImportGlobals) NewPtrClear(sizeof(WebMImportGlobalsRec));
  if ((err = MemError()) == noErr) {
    store->self = self;
    SetComponentInstanceStorage(self, (Handle)store);
  }

  dbg_printf("[WebM Import] <   [%08lx] :: Open()\n", (UInt32) store);
  return err;
}


//--------------------------------------------------------------------------------
// Component Close Request - Required
pascal ComponentResult WebMImportClose(WebMImportGlobals store, ComponentInstance self)
{
  dbg_printf("[WebM Import]  >> [%08lx] :: Close()\n", (UInt32) store);

  if (store)
    DisposePtr((Ptr)store);

  dbg_printf("[WebM Import] <   [%08lx] :: Close()\n", (UInt32) store);
  return noErr;
}


//--------------------------------------------------------------------------------
// Component Version Request - Required
pascal ComponentResult WebMImportVersion(WebMImportGlobals store)
{
  dbg_printf("[WebM Import]  >> [%08lx] :: Version()\n", (UInt32) store);
  dbg_printf("[WebM Import] <   [%08lx] :: Version()\n", (UInt32) store);
  return kWebMImportVersion;
}


//--------------------------------------------------------------------------------
// MovieImportFile
//
pascal ComponentResult WebMImportFile(WebMImportGlobals store, const FSSpec *theFile,
                                      Movie theMovie, Track targetTrack, Track *usedTrack,
                                      TimeValue atTime, TimeValue *durationAdded,
                                      long inFlags, long *outFlags)
{
  OSErr err = noErr;
  AliasHandle alias = NULL;
  *outFlags = 0;

  dbg_printf("[WebM Import]  >> [%08lx] :: FromFile(%d, %ld)\n", (UInt32) store, targetTrack != NULL, atTime);

  err = NewAliasMinimal(theFile, &alias);
  if (!err) {
    err = MovieImportDataRef(store->self, (Handle)alias, rAliasType, theMovie, targetTrack, usedTrack, atTime, durationAdded, inFlags, outFlags);

  }
  else {
    if (alias)
      DisposeHandle((Handle)alias);
  }

  dbg_printf("[WebM Import] <   [%08lx] :: FromFile()\n", (UInt32) store);
  return err;
}


//--------------------------------------------------------------------------------
// MovieImportDataRef
//
// Initial algorithm will iterate through the WebM file:
//  - calculate duration of previous video block
//  - add it to Quicktime movie
//  - collect audio block references
// Then loop through audio block references:
//  - calculate duration of each audio block
//  Add all audio block references to quicktime movie
//
// A better algorithm is to implement an Idling Importer
// that can begin playing before parsing the entire file.
// http://developer.apple.com/library/mac/#technotes/tn2004/tn2111.html
// NOTE: Toggle between idling and non-idling importer using canMovieImportWithIdle flag in the .r file.
//
// QuickTime calls:
// NewMovieTrack()
// NewTrackMedia()
// SetTrackEnabled()
// while {
//    AddMediaSampleReference()
//    incr to next frame
// InsertMediaIntoTrack()
// GetTrackDuration()
//
pascal ComponentResult WebMImportDataRef(WebMImportGlobals store, Handle dataRef, OSType dataRefType,
                                         Movie theMovie, Track targetTrack, Track *usedTrack,
                                         TimeValue atTime, TimeValue *durationAdded,
                                         long inFlags, long *outFlags)
{
  OSErr err = noErr;
  store->idleManager = NULL;
  store->movie = theMovie;
  store->dataRef = dataRef;
  store->dataRefType = dataRefType;
  store->audioDescHand = NULL;
  store->vp8DescHand = NULL;
  store->loadState = kMovieLoadStateLoading;
  store->videoMaxLoaded = 0;
  store->addSamplesLast = 0;
  store->reader = NULL;
  ComponentInstance dataHandler = 0;
  const long long ns_per_sec = 1000000000;  // conversion factor, ns to sec
  TimeValue mediaDuration, audioMediaDuration;
  const mkvparser::Cluster* webmCluster = NULL;
  mkvparser::Cluster* nextCluster = NULL;

  dbg_printf("[WebM Import]  >> [%08lx] :: FromDataRef(dataRef = %08lx, atTime = %ld)\n", (UInt32) store, (UInt32) dataRef, atTime); // targetTrack != NULL

  // The movieImportMustUseTrack flag indicates that we must use an existing track.
  // We don't support this and always create a new track, so return paramErr.
  if (inFlags & movieImportMustUseTrack)
    return paramErr;

  // Use IMkvReader subclass that knows about quicktime dataRef and dataHandler objects, rather than plain file io.
  // allocate reader on heap so it doesn't go out of scape at end of ImportDataRef().
  long long pos = 0;
  MkvBufferedReaderQT* reader = (MkvBufferedReaderQT*) new MkvBufferedReaderQT;
  if (reader == NULL)
    return invalidDataRef;
  int status = reader->Open(dataRef, dataRefType);
  if (status != 0) {
    dbg_printf("[WebM Import] MkvReaderQT::Open() Error, status = %d\n", status);
    return invalidDataRef;
  }
  store->reader = reader;

  // Preload the io buffer synchronously.
  reader->InitBuffer();

  long long totalLength = 0;
  long long availLength = 0;
  reader->Length(&totalLength, &availLength);
  dbg_printf("MkvReaderQT::m_length = %lld bytes. availableLength = %lld\n", totalLength, availLength);

  // Use the libwebm project (libmkvparser.a) to parse the WebM file.

  //
  // WebM Header
  //
  using namespace mkvparser;
  EBMLHeader ebmlHeader;
  long long headerstatus = ebmlHeader.Parse(reader, pos);
  if (headerstatus != 0) {
    dbg_printf("[WebM Import] EBMLHeader.Parse() Error, returned %lld.\n", headerstatus );
    return -1;  // or maybe invalidDataRef
  }

  //
  // WebM Segment
  //
  mkvparser::Segment* webmSegment;
  long long ret = mkvparser::Segment::CreateInstance(reader, pos, webmSegment);   // pass ownership of reader object to Segment
  if (ret) {
    dbg_printf("Segment::CreateInstance() failed.\n");
    return -1;
  }

  DataHTask(store->dataHandler);

#if USE_PARSE_HEADERS
  // Use ParseHeaders instead of Load(). Test performance.
  ret = webmSegment->ParseHeaders();
#else
  // Load the WebM Segment.
  ret = webmSegment->Load();
  if (ret) {
    dbg_printf("Segment::Load() failed.\n");
    return -1;
  }

  const unsigned long numClusters = webmSegment->GetCount();
  if (numClusters == 0) {
    dbg_printf("WebM Import - Segment has no Clusters!\n");
    delete webmSegment;
    return -1;
  }
  dbg_printf("Number of Clusters: %ld\n", numClusters);
#endif
  store->webmSegment = webmSegment;


  //
  // WebM SegmentInfo
  //
  const SegmentInfo* const webmSegmentInfo = webmSegment->GetInfo();
  const long long segmentDuration = webmSegmentInfo->GetDuration();
  store->segmentDuration = segmentDuration;

  //
  // WebM Tracks
  //
  const mkvparser::Tracks* const webmTracks = webmSegment->GetTracks();
  store->webmTracks = webmTracks;

  // Use the WebM Tracks info to create QT Track and QT Media up front.  Don't wait for first video Block object.
  unsigned long trackIndex = 0;                 // track index is 0-based
  const unsigned long numTracks = webmTracks->GetTracksCount();
  while (trackIndex < numTracks) {
    const mkvparser::Track* const webmTrack = webmTracks->GetTrackByIndex(trackIndex++);
    if (webmTrack == NULL)
      continue;
    unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
    if (trackType == VIDEO_TRACK) {
      const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const mkvparser::VideoTrack* const>(webmTrack);
      long long width = webmVideoTrack->GetWidth();
      long long height = webmVideoTrack->GetHeight();

      // Create image description so QT can find appropriate decoder component (our VP8 decoder)
      if (store->vp8DescHand == NULL) {
        err = CreateVP8ImageDescription(width, height, &store->vp8DescHand);
        if (err) return -1; //goto bail;
      }

      // Create a QT movie track (not WebM file track), a QT media object, and enable the track.
      if (store->movieVideoTrack == NULL) {
        // Create a new QT video track
        store->movieVideoTrack = NewMovieTrack(theMovie, (**store->vp8DescHand).width << 16, (**store->vp8DescHand).height << 16, kNoVolume);
        store->trackCount++;

        // Create a new QT media for the track
        // (The media refers to the actual data samples used by the track.)
        TimeScale timeScale = GetMovieTimeScale(theMovie);  // QT uses 600 units per second
        store->movieVideoMedia = NewTrackMedia(store->movieVideoTrack, VideoMediaType, timeScale, dataRef, dataRefType);
        if (err = GetMoviesError()) return -1; //goto bail;
        dbg_printf("timeScale passed to NewTrackMedia is: %ld\n", timeScale);

        // Enable the track.
        SetTrackEnabled(store->movieVideoTrack, true);
      } // end quicktime movie track
    }

    if (trackType == AUDIO_TRACK) {
      const mkvparser::AudioTrack* const webmAudioTrack = static_cast<const mkvparser::AudioTrack* const>(webmTrack);

      // Store sampling rate in component global for use by FinishAddingAudioBlocks().
      if (store->audioSampleRate == 0) {
        store->audioSampleRate = webmAudioTrack->GetSamplingRate();
      }

      // Create sound description so QT can find appropriate codec component for decoding the audio data.
      if (store->audioDescHand == NULL) {
        err = CreateAudioDescription(&store->audioDescHand, webmAudioTrack);
        if (err) {
          dbg_printf("WebM Import - CreateAudioDescription() Failed with %d.\n", err);
          return -1;
        }
      }

      // Create a QT movie track object, a QT media object, and enable the track.
      if (store->movieAudioTrack == NULL) {
        // Create a new QT audio track
        store->movieAudioTrack = NewMovieTrack(store->movie, 0, 0, kFullVolume); // pass 0 for width and height of audio track.
        if (store->movieAudioTrack == NULL)
          return -1;

        store->trackCount++;

        // Create a new QT media for the track
        long sampleRate = 0;
        //sampleRate = GetMovieTimeScale(store->movie);
        sampleRate = webmAudioTrack->GetSamplingRate();
        store->movieAudioMedia = NewTrackMedia(store->movieAudioTrack, SoundMediaType, sampleRate, store->dataRef, store->dataRefType);
        if (err = GetMoviesError())
          return -1;

        // Enable the track.
        SetTrackEnabled(store->movieAudioTrack, true);
      }

    }
  }

  // Any other level one elements in webm: Chapters, Attachments, MetaSeek, ?

  // Print debug info on the WebM header, segment, and tracks information.
  DumpWebMDebugInfo(&ebmlHeader, webmSegmentInfo, webmTracks);

  // if no tracks, then nothing to do here...
  if (store->trackCount == 0)
    return noErr;

  if (inFlags & movieImportWithIdle) {
    //
    // IDLING IMPORTER
    //
    dbg_printf("IDLING IMPORTER\n");
    store->usingIdles = true;

    // create placeholder track
    // ****

    //
    // WebM Cluster - add one cluster
    //
#if USE_PARSE_HEADERS
    ret = webmSegment->LoadCluster();
#endif
    webmCluster = webmSegment->GetFirst();
    store->webmCluster = webmCluster;
    err = AddCluster(store, webmCluster, webmTracks, segmentDuration);  // need tracks object to get track for each block
    *outFlags |= movieImportResultNeedIdles;
    return noErr;
  }
  else {
    //
    // NON-IDLING IMPORTER
    //
    dbg_printf("NON-IDLING IMPORTER\n");
    store->usingIdles = false;

    store->webmCluster = NULL;

    //
    //  WebM Cluster - Loop for each cluster in the file
    //
    long long lastTime_ns = 0;
    webmCluster = webmSegment->GetFirst();
    while ((webmCluster != NULL) && !webmCluster->EOS()) {
      lastTime_ns = segmentDuration;  // webmCluster is last cluster in segment, so pass in duration of whole segment.

      // Add Cluster to quicktime movie.
      AddCluster(store, webmCluster, webmTracks, lastTime_ns);

      // Advance to next webm Cluster
      webmCluster = webmSegment->GetNext(webmCluster);
    }

    // Add any remaining samples.
    if (store->videoSamples.size() > 0)
      FinishAddingVideoBlocks(store, segmentDuration);
    if (store->audioSamples.size() > 0)
      FinishAddingAudioBlocks(store, segmentDuration);

    // Return the duration added
    if (store->movieVideoTrack != NULL) {
      *durationAdded = GetTrackDuration(store->movieVideoTrack) - atTime;
//      *outFlags |= movieImportCreateTrack;
    }
#if 0
    else if (store->movieAudioTrack != NULL) {
      *durationAdded = GetTrackDuration(store->movieAudioTrack) - atTime;
      *outFlags |= movieImportCreateTrack;
    }
#endif
    // Return the track identifier of the track that received the imported data in the usedTrack pointer. Your component
    // needs to set this parameter only if you operate on a single track or if you create a new track. If you modify more
    // than one track, leave the field referred to by this parameter unchanged.
    if (usedTrack && store->trackCount < 2)
      *usedTrack = store->movieVideoTrack ? store->movieVideoTrack : store->movieAudioTrack;

    store->loadState = kMovieLoadStateComplete;
  }

  dbg_printf("durationAdded = %ld\n", *durationAdded );
  dbg_printf("[WebM Import]  << [%08lx] :: FromDataRef(%d, %ld)\n", (UInt32) store, targetTrack != NULL, atTime);

  return err;
}


//--------------------------------------------------------------------------------
// MovieImportGetMimeTypeList
pascal ComponentResult WebMImportGetMIMETypeList(WebMImportGlobals store, QTAtomContainer *outMimeInfo)
{
  dbg_printf("[WebM Import]  >> [%08lx] :: GetMIMETypeList()\n", (UInt32) store);
  OSErr err = GetComponentResource((Component)store->self, 'mime', 263, (Handle *)outMimeInfo);
  if (err != noErr) {
    dbg_printf("GetMIMETypeList FAILED");
  }
  dbg_printf("[WebM Import]  << [%08lx] :: GetMIMETypeList()\n", (UInt32) store);

  return err;
}


//--------------------------------------------------------------------------------
// MovieImportValidateDataRef
pascal ComponentResult WebMImportValidateDataRef(WebMImportGlobals store, Handle dataRef, OSType dataRefType, UInt8 *valid)
{
  OSErr err = noErr;
  dbg_printf("[WebM Import]  >> [%08lx] :: ValidateDataRef()\n", (UInt32) store);
  MkvReaderQT* reader = (MkvReaderQT*) new MkvReaderQT;
  int status = reader->Open(dataRef, dataRefType);
  if (status == 0) {
    *valid = 128;
    err = noErr;
  }
  else {
    dbg_printf("[WebM Import] ValidateDataRef() FAIL... err = %d\n", err);
    err = invalidDataRef;
  }

  dbg_printf("[WebM Import]  << [%08lx] :: ValidateDataRef()\n", (UInt32) store);
  return err;
}


//--------------------------------------------------------------------------------
// MovieImportValidate
pascal ComponentResult WebMImportValidate(WebMImportGlobals store, const FSSpec *theFile, Handle theData, Boolean *valid)
{
  OSErr err = noErr;

  dbg_printf("[WebM Import]  >> [%08lx] :: Validate()\n", (UInt32) store);
  FSRef fileFSRef;
  AliasHandle fileAlias = NULL;
  *valid = false;

  if ((err = FSpMakeFSRef(theFile, &fileFSRef) == noErr) &&
      (err = FSNewAliasMinimal(&fileFSRef, &fileAlias) == noErr)) {
    err = MovieImportValidateDataRef(store->self,  (Handle)fileAlias, rAliasType, (UInt8*) valid);
  }

  if (*valid == false) {
    dbg_printf("[WebM Import] Validate() FAIL... \n");
  }

  dbg_printf("[WebM Import]  << [%08lx] :: Validate()\n", (UInt32) store);
  return err;
}


// MovieImportRegister


//--------------------------------------------------------------------------------
pascal ComponentResult WebMImportIdle(WebMImportGlobals store, long inFlags, long* outFlags)
{
  dbg_printf("WebMImportIdle()\n");
  DumpWebMGlobals(store);

  //DataHTask(store->dataHandler);

  // get next cluster
  const mkvparser::Cluster* webmCluster = store->webmSegment->GetNext(store->webmCluster);
  delete store->webmCluster;
  store->webmCluster = webmCluster;
  //store->webmCluster = store->webmSegment->GetNext(store->webmCluster);

  // import the cluster
  if ((store->webmCluster != NULL) && !store->webmCluster->EOS()) {
    AddCluster(store, store->webmCluster, store->webmTracks, store->segmentDuration);
#if USE_PARSE_HEADERS
    // AddCluster() calls LoadCluster() just before return so its ready for next idle call.
    // Otherwise we would have to call it here: long long ret = store->webmSegment->LoadCluster();
#endif
    store->reader->MkvBufferedReaderQT::ReadAsync(store->reader);  // restart pump if stalled.
  }
  else {
    // If no more clusters, add any remaining samples from cache.
    if (store->videoSamples.size() > 0)
      FinishAddingVideoBlocks(store, store->segmentDuration);
    if (store->audioSamples.size() > 0)
      FinishAddingAudioBlocks(store, store->segmentDuration);

    // set flags to indicate we are done.
    *outFlags |= movieImportResultComplete;
    store->loadState = kMovieLoadStateComplete;
  }

  return noErr;
}

//--------------------------------------------------------------------------------
pascal ComponentResult WebMImportSetIdleManager(WebMImportGlobals store, IdleManager idleMgr)
{
  store->idleManager = idleMgr;
  return noErr;
}

//--------------------------------------------------------------------------------
pascal ComponentResult WebMImportGetMaxLoadedTime(WebMImportGlobals store, TimeValue *time)
{
  // time arg is in movie's timescale so return video maxloaded
  *time = store->videoMaxLoaded;
  return noErr;
}

//--------------------------------------------------------------------------------
//  Returns the asynchronous load state for a movie.
pascal ComponentResult WebMImportGetLoadState(WebMImportGlobals store, long* importerLoadState)
{
  *importerLoadState = store->loadState;
  return noErr;
}



#pragma mark -

//--------------------------------------------------------------------------------
OSErr AddCluster(WebMImportGlobals store, const mkvparser::Cluster* webmCluster, const mkvparser::Tracks* webmTracks, long long lastTime_ns)
{
  OSErr err = noErr;

  const long long timeCode = webmCluster->GetTimeCode();
  const long long time_ns = webmCluster->GetTime();
  // dbg_printf("TIME - Cluster Time: %lld\n", time_ns);

  // Loop for each Block in Cluster
  const mkvparser::BlockEntry* webmBlockEntry = webmCluster->GetFirst();
  while ((webmBlockEntry != NULL) && (!webmBlockEntry->EOS()))
  {
    //
    //  WebM Block
    //

    const mkvparser::Block* const webmBlock = webmBlockEntry->GetBlock();
    const long long blockTime_ns = webmBlock->GetTime(webmCluster);

    const unsigned long trackNum = webmBlock->GetTrackNumber(); // block's track number (see
    const mkvparser::Track* webmTrack = webmTracks->GetTrackByNumber(trackNum);
    const unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
    if (trackType == VIDEO_TRACK) {
      //
      // WebM Video Data
      //
      const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const mkvparser::VideoTrack* const>(webmTrack);
      err = AddVideoBlock(store, webmBlock, blockTime_ns, webmVideoTrack);
    } // end video track
    else if (trackType == AUDIO_TRACK) {
      //
      // WebM Audio Data
      //
      const mkvparser::AudioTrack* const webmAudioTrack = static_cast<const mkvparser::AudioTrack* const>(webmTrack);
      err = AddAudioBlock(store, webmBlock, blockTime_ns, webmAudioTrack);
    }

    // Advance to next webm Block within this Cluster.
    webmBlockEntry = webmCluster->GetNext(webmBlockEntry);
  } // end block loop

  if (!store->usingIdles || ShouldAddSamples(store)) {
    // TODO(arkadini@gmail.com): consider parameterizing and merging
    // into one the following two, very similar, functions?
    FinishAddingVideoBlocks(store, lastTime_ns);
    FinishAddingAudioBlocks(store, lastTime_ns);
    store->loadState = kMovieLoadStatePlayable;
    store->addSamplesLast = TickCount();
  }

#if USE_PARSE_HEADERS
  long long ret = store->webmSegment->LoadCluster();
#endif

  return err;
}


//--------------------------------------------------------------------------------
// CreateVP8ImageDescription
// Create image description for a VP8 video frame so that QT can find codec component to decode it.
// see ImageCompression.h
OSErr CreateVP8ImageDescription(long width, long height, ImageDescriptionHandle *descOut)
{
  OSErr err = noErr;
  ImageDescriptionHandle descHand = NULL;
  ImageDescriptionPtr descPtr;

  descHand = (ImageDescriptionHandle)NewHandleClear(sizeof(ImageDescription));
  if (err = MemError()) goto bail;

  descPtr = *descHand;
  descPtr->idSize = sizeof(ImageDescription);     // total size of this image description structure
  descPtr->cType = kVP8CodecFormatType;           // type of compressor component that created this compressed data
  descPtr->vendor = kGoogManufacturer;            // developer of compressor that created compressed image
  descPtr->frameCount = 1;                        // number of frames in the image data
  descPtr->depth = codecInfoDepth24;              // pixel depth specified for the compressed image
  descPtr->clutID = -1;                           // id of color table for compressed image, -1 if color table not used
  descPtr->hRes = 72L << 16;                      // horizontal resolution dpi
  descPtr->vRes = 72L << 16;                      // vertical resolution dpi
  descPtr->width = width;                         // image width in pixels (passed into this routine)
  descPtr->height = height;                       // image height in pixels
  descPtr->version = 0;                           // version of the compressed data

bail:
  if (descHand && err) {
    DisposeHandle((Handle)descHand);
    descHand = NULL;
  }

  *descOut = descHand;

  return err;
}


enum {
  kAudioFormatVorbis = 'XiVs'
};


//--------------------------------------------------------------------------------
// CreateAudioDescription
//  Create audio sample description for Vorbis.
OSErr CreateAudioDescription(SoundDescriptionHandle *descOut, const mkvparser::AudioTrack* webmAudioTrack)
{
  OSErr err = noErr;
  SoundDescriptionHandle descHand = NULL;
  unsigned long cookieSize = 0;
  Handle cookieHand = NULL;
  Ptr cookie = NULL;

  err = CreateCookieFromCodecPrivate(webmAudioTrack, &cookieHand);
  if ((err == noErr) && (cookieHand)) {
    cookie = *cookieHand;
    cookieSize = GetHandleSize(cookieHand);
  }

  // In all fields, a value of 0 indicates that the field is either unknown, not applicable or otherwise is inapproprate for the format and should be ignored.
  AudioStreamBasicDescription asbd;
  asbd.mFormatID = kAudioFormatVorbis;  // kAudioFormatLinearPCM;
  asbd.mSampleRate = webmAudioTrack->GetSamplingRate();       // 48000.0 or whatever your sample rate is.  AudioStreamBasicDescription.mSampleRate is of type Float64.
  asbd.mFormatFlags = kAudioFormatFlagIsBigEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked; // or leave off big endian if it's LittleEndian
  asbd.mChannelsPerFrame = webmAudioTrack->GetChannels();     // 1;
  asbd.mBitsPerChannel = 0;       // 24; // or webmAudioTrack->GetBitDepth() ?
  asbd.mFramesPerPacket = 0;      // 1;
  asbd.mBytesPerFrame = 0;        // 3;
  asbd.mBytesPerPacket = 0;       // 3;

  dbg_printf("WebMImport CreateAudioDescription() - asbd.mSampleRate = %7.3f\n", asbd.mSampleRate);
  err = QTSoundDescriptionCreate(&asbd,     // description of the format
                                 NULL,      // AudioChannelLayout, NULL if there isn't one
                                 0,         // ByteCount, size of audio channel layout, (0 if above is NULL)
                                 (void*)cookie,      // magic cookie - ptr to qt atoms containing Vorbis codec private data, or NULL.
                                 cookieSize,         // magic cookie size - size of data pointed to by cookie arg, or 0 if NULL.
                                 kQTSoundDescriptionKind_Movie_LowestPossibleVersion,   // QTSoundDescriptionKind
                                 &descHand);     // out, sound description
  if (err == noErr) {
    *descOut = descHand;
  }

  long convertedSR = (*descHand)->sampleRate >> 16; // convert unsigned fixed to long (whole part)
  dbg_printf("WebMImport CreateAudioDescription() - descHand.sampleRate = %ld\n", convertedSR); // Note: SoundDescription.sampleRate is of type UnsignedFixed
  return err;
}

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

//--------------------------------------------------------------------------------
//  CreateCookieFromCodecPrivate
//
// The Matroska spec <http://www.matroska.org/technical/specs/codecid/index.html> describes the Vorbis codec private data as:
// The private data contains the first three Vorbis packet in order. The lengths of the packets precedes them. The actual layout is:
// Byte 1: number of distinct packets '#p' minus one inside the CodecPrivate block. This should be '2' for current Vorbis headers.
// Bytes 2..n: lengths of the first '#p' packets, coded in Xiph-style lacing. The length of the last packet is the length of the CodecPrivate block minus the lengths coded in these bytes minus one.
// Bytes n+1..: The Vorbis identification header, followed by the Vorbis comment header followed by the codec setup header.
//
OSErr CreateCookieFromCodecPrivate(const mkvparser::AudioTrack* webmAudioTrack, Handle* cookie)
{
  OSErr err = noErr;
  Handle cookieHand = NULL;

  size_t vorbisCodecPrivateSize;
  const unsigned char* vorbisCodecPrivateData = webmAudioTrack->GetCodecPrivate(vorbisCodecPrivateSize);
  dbg_printf("Vorbis Codec Private Data = %p, Vorbis Codec Private Size = %ld\n", vorbisCodecPrivateData, vorbisCodecPrivateSize);

  int numPackets = vorbisCodecPrivateData[0] + 1;
  const unsigned long idHeaderSize = vorbisCodecPrivateData[1];
  const unsigned long commentHeaderSize = vorbisCodecPrivateData[2];
  const unsigned char* idHeader = &vorbisCodecPrivateData[3];
  const unsigned char* commentHeader = idHeader + idHeaderSize;
  const unsigned char* setupHeader =  commentHeader + commentHeaderSize;
  const unsigned long setupHeaderSize = vorbisCodecPrivateSize - idHeaderSize - commentHeaderSize - 2 - 1;  // calculate size of third packet
  // size of third packet = total size minus length of other two packets, minus the two length bytes, minus 1 for the "numPackets" byte.
  if ((3 + idHeaderSize + commentHeaderSize + setupHeaderSize) != vorbisCodecPrivateSize) {
    dbg_printf("Error. Codec Private header sizes don't add up. 3 + id (%ld) + comment (%ld) + setup (%ld) != total (%ld)\n", idHeaderSize, commentHeaderSize, setupHeaderSize, vorbisCodecPrivateSize);
    return -1;
  }

  // Note: CookieAtomHeader is {long size; long type; unsigned char data[1]; } see WebMAudioStream.c

  // first packet - id header
  uint32_t atomhead1[2] = { EndianU32_NtoB(idHeaderSize + 2*4), EndianU32_NtoB(kCookieTypeVorbisHeader) };
  PtrToHand(atomhead1, &cookieHand, sizeof(atomhead1));
  PtrAndHand(idHeader, cookieHand, idHeaderSize);

  // second packet - comment header
  uint32_t atomhead2[2] = { EndianU32_NtoB(commentHeaderSize + sizeof(atomhead2)), EndianU32_NtoB(kCookieTypeVorbisComments) };
  PtrAndHand(atomhead2, cookieHand, sizeof(atomhead2));
  PtrAndHand(commentHeader, cookieHand, commentHeaderSize);

  // third packet - setup header
  uint32_t atomhead3[2] = { EndianU32_NtoB(setupHeaderSize + sizeof(atomhead3)), EndianU32_NtoB(kCookieTypeVorbisCodebooks) };
  PtrAndHand(atomhead3, cookieHand, sizeof(atomhead3));
  PtrAndHand(setupHeader, cookieHand, setupHeaderSize);

  *cookie = cookieHand;
  return noErr;
}


//--------------------------------------------------------------------------------
//  AddAudioBlock()
//  Add one audio block to cache of sample references.
//  Call this for each block in a section (maybe a cluster) to collect sample references first,
//  and then call AddMediaSampleReferences() later for the section, or once at the end of file.
//
//  Lacing means there could be multiple Frames per Block, so iterate and add all Frames here.
//
OSErr AddAudioBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns, const mkvparser::AudioTrack* webmAudioTrack)
{
  OSErr err = noErr;
  dbg_printf("Audio Block\n");

  long frameCount = webmBlock->GetFrameCount();
  for (long fi = 0; fi < frameCount; fi++) {
    const mkvparser::Block::Frame& webmFrame = webmBlock->GetFrame(fi);  // zero-based index
    dbg_printf("\tFrame:\tpos:%15lld, len:%15ld\n", webmFrame.pos, webmFrame.len);

    SampleReferencePtr srp;
    srp = (SampleReferencePtr)malloc(sizeof(SampleReferenceRecord));
    srp->dataOffset = webmFrame.pos;          // webmBlock->GetOffset();
    srp->dataSize = webmFrame.len;            // webmBlock->GetSize();
    srp->durationPerSample = 0;               // need to calculate this later for all samples.
    srp->numberOfSamples = 1;
    srp->sampleFlags = 0; // ?

    store->audioSamples.push_back(*srp);
    store->audioTimes.push_back(blockTime_ns);  // blockTime_ns needs to be passed into AddAudioBlock(), or pass in webmCluster so we can call webmBlock->GetTime(webmCluster); here.
  }

  return err;
}


//--------------------------------------------------------------------------------
//  FinishAddingAudioBlocks
// Add some audio sample references to the QuickTime media.
// The argument lastTime_ns will be timestamp of next sample that is not in store->audioSamples, or it will be set to duration of entire movie.
// Assume that sample references for a section of audio blocks have already been added to samples list store->audioSamples.
// This "section" may be all blocks in one cluser, or all blocks within some arbitrary time duration (like every 5 seconds).
//
OSErr FinishAddingAudioBlocks(WebMImportGlobals store, long long lastTime_ns)
{
  OSErr err = noErr;
  const long long ns_per_sec = 1000000000;  // conversion factor, ns to sec
  TimeValue accSectionDuration_qt = 0;

  // Note: (*store->audioDescHand)->sampleRate is of type UnsignedFixed (not long, not float or double)
  TimeScale mediaTimeScale = store->audioSampleRate;  // audio
  // TimeScale mediaTimeScale = GetMovieTimeScale(store->movie)); // video

  //
  // Calculate duration value for each sample
  //
  long numSamples = store->audioSamples.size();
  long numTimes = store->audioTimes.size();
  if (numSamples != numTimes) {
    dbg_printf("WebM Import - FinishAddingAudioBlocks - ERROR - parallel vectors with different sizes. numSamples=%ld, numTimes=%ld\n", numSamples, numTimes);
    // try to recover ****
  }
  long numSamplesToAdd = 0;
  if (numSamples <= 0)
    return 0;
  else if (numSamples == 1)
    numSamplesToAdd = 1;                   // add last audio block in the file.
  else
    numSamplesToAdd = (numSamples - 1);    // try deferring the last block in the cache

  dbg_printf("WebM Import - FinishAddingAudioBlocks - numSamples = %ld, numTimes = %ld, numSamplesToAdd=%ld, lastTime_ns = %lld\n", numSamples, numTimes, numSamplesToAdd, lastTime_ns);

  long long blockDuration_ns = 0;
  for (long i = 0; i < numSamplesToAdd; ++i) {
    if (i+1 < numSamples)
      blockDuration_ns = (store->audioTimes[i+1] - store->audioTimes[i]);
    else
      blockDuration_ns = (lastTime_ns - store->audioTimes[i]);
    if (blockDuration_ns < 0)
      blockDuration_ns = 0; // catch invalid duration

    if (store->audioTimes[i] > lastTime_ns) {
      dbg_printf("WebM Import - Bogus webm file: Audio Block with timestamp > segment duration.\n");
    }
    TimeValue blockDuration_qt = static_cast<TimeValue>(double(blockDuration_ns) / ns_per_sec * mediaTimeScale);
    store->audioSamples[i].durationPerSample = blockDuration_qt;  // TimeValue, count of units
    accSectionDuration_qt += blockDuration_qt;    // accumulate duration of section in media time scale

    dbg_printf("\t\t\t i=%ld, Time=%lld, Duration_ns=%lld, Duration_qt=%ld, mediaTimeScale=%ld\n", i, store->audioTimes[i], blockDuration_ns, store->audioSamples[i].durationPerSample, mediaTimeScale);
    // Alternatively, we can call AddMediaSampleReference() here inside loop, for each block. Supposedly more efficient to call it once after loop, specifying all blocks.
  }

  //
  // Add array of sample references to Media all at once (better performance than adding each one separately).
  //
  if (numSamplesToAdd > 0) {
    SampleReferencePtr sampleRefs = &store->audioSamples.front(); // &store->audioSamples[0];
    err = AddMediaSampleReferences(store->movieAudioMedia, (SampleDescriptionHandle)store->audioDescHand, numSamplesToAdd, sampleRefs, NULL);  // SampleReferencePtr
    if (err) {
      dbg_printf("WebM Import - FinishAddingAudioBlocks - AddMediaSampleRefereces() FAILED with err = %d\n", err);
    }
    store->audioCount += numSamplesToAdd;  // increment count when samples are actually added to qt
  }

  // Insert Media into Track
  // A "section" below is the collection of sample references corresponding to the blocks of one cluster.

  long long sectionStart_ns, sectionEnd_ns, sectionDuration_ns = 0;
  TimeValue sectionStart_qt, sectionDuration_qt = 0;

  sectionStart_ns = store->audioTimes[0]; // or if we dont delete the refs, use store->audioTimes[sectionStartIndex];
  sectionStart_qt = static_cast<TimeValue>(double(sectionStart_ns) / ns_per_sec * mediaTimeScale);
  //sectionEnd_ns = lastTime_ns;  // arg passed in
  //sectionDuration_ns = sectionEnd_ns - sectionStart_ns;
  //sectionDuration_qt = static_cast<TimeValue>(double(sectionDuration_ns) / ns_per_sec * mediaTimeScale);
  //if (sectionDuration_qt != accSectionDuration_qt)
  //  dbg_printf("FinishAddingAudioBlocks duration mismatch.  accSecDur_qt=%ld != secDur_qt=%ld\n", accSectionDuration_qt, sectionDuration_qt);
  TimeValue trackStart = -1;  // -1 means insert the media at the END of the track
  TimeValue mediaTime = sectionStart_qt; // starting point of media section to insert, expressed in media's time scale.
  TimeValue mediaDuration = accSectionDuration_qt;  // duration of media section, expressed in media's time scale.
  dbg_printf("audioMaxLoaded=%ld, sectionStart_qt=%ld\n", store->audioMaxLoaded, sectionStart_qt); // test, use maxLoaded in call to Insert instead of calculated value here.
  mediaTime = store->audioMaxLoaded;  // use actual duration value for frames already loaded, rather than calculating it now. Avoid -2014 error from Insert call below.
  err = InsertMediaIntoTrack(store->movieAudioTrack, trackStart, mediaTime, mediaDuration, fixed1);
  if (err)
    dbg_printf("InsertMediaIntoTrack FAIL with err = %d\n", err);
  dbg_printf("InsertMediaIntoTrack(audioTrack, trackStart=%ld, mediaTime=%ld, mediaDuration=%ld,)\n", trackStart, mediaTime, mediaDuration);
  store->audioMaxLoaded += mediaDuration;

  //
  // Clean up cache
  // Delete sample refs and times after they have been added to quicktime (or maintain ptr into vector)
  //
  store->audioSamples.erase(store->audioSamples.begin(), store->audioSamples.begin()+numSamplesToAdd);  // stl vector.erase(first, last) will not erase last. Range is [first,last).
  store->audioTimes.erase(store->audioTimes.begin(), store->audioTimes.begin()+numSamplesToAdd);

  dbg_printf("FINISH at end of func, size of audio sample vector is: %ld\n", store->audioSamples.size());

  return err;
}


//--------------------------------------------------------------------------------
//  AddVideoBlock
//  Add one video block to cache of sample references.
//  Call this for each block in a section (maybe a cluster) to collect sample references first,
//  and then call AddMediaSampleReferences() later for the section, or once at the end of file.
//
//  Lacing means there could be multiple Frames per Block, so iterate and add all Frames here.
//
OSErr AddVideoBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns, const mkvparser::VideoTrack* webmVideoTrack)
{
  OSErr err = noErr;
  dbg_printf("Video Block\n");

  long frameCount = webmBlock->GetFrameCount();
  for (long fi = 0; fi < frameCount; fi++) {
    const mkvparser::Block::Frame& webmFrame = webmBlock->GetFrame(fi);  // zero-based index
    dbg_printf("\tFrame:\tpos:%15lld, len:%15ld\n", webmFrame.pos, webmFrame.len);

    SampleReferencePtr srp;
    srp = (SampleReferencePtr)malloc(sizeof(SampleReferenceRecord));
    srp->dataOffset = webmFrame.pos;          // webmBlock->GetOffset();
    srp->dataSize = webmFrame.len;            // webmBlock->GetSize();
    srp->durationPerSample = 0;               // need to calculate this later for all samples.
    srp->numberOfSamples = 1;
    srp->sampleFlags = 0;                     //
    if (!webmBlock->IsKey())
      srp->sampleFlags |= mediaSampleNotSync;

    store->videoSamples.push_back(*srp);
    store->videoTimes.push_back(blockTime_ns);  // blockTime_ns needs to be passed into AddAudioBlock(), or pass in webmCluster so we can call webmBlock->GetTime(webmCluster); here.
  }

  //  dbg_printf("Video Block: (offset=%ld, size=%ld, duration=calculated later.\n", srp->dataOffset, srp->dataSize);
  return err;
}


//--------------------------------------------------------------------------------
//  FinishAddingVideoBlocks
//  Add a section of video sample references to the quicktime media.
//  The section may be all blocks in one cluser, or all blocks within some arbitrary time duration (like every 5 seconds).
//
//  Cannot calculate the duration of the last block in a cluster.  Tried to have "lastTime_ns" arg passed in as time of next video block (first video block on next cluster)
//  but doesn't work because first block of next cluster is usually an audio block, not video block.  Also, inefficient to keep looking ahead.
//  Instead, try adding all blocks except the last one in the cache.  Leave last block in the cache (std::vector) with zero duration.  It will be added with blocks of next cluster later.
//
OSErr FinishAddingVideoBlocks(WebMImportGlobals store, long long lastTime_ns)
{
  OSErr err = noErr;
  TimeValue accSectionDuration_qt = 0;
  TimeScale mediaTimeScale = GetMovieTimeScale(store->movie); // video

  //
  // Calculate duration value for each sample in section
  //
  long numSamples = store->videoSamples.size();
  long numTimes = store->videoTimes.size();
  if (numSamples != numTimes) {
    dbg_printf("WebM Import - FinishAddingVideoBlocks - ERROR - parallel vectors with different sizes. numSamples=%ld, numTimes=%ld\n", numSamples, numTimes);
    // try to recover ****
  }
  long numSamplesToAdd = 0;
  if (numSamples <= 0)
    return 0;
  else if (numSamples == 1)
    numSamplesToAdd = 1;                   // add last video block in the file.
  else
    numSamplesToAdd = (numSamples - 1);    // try deferring the last block in the cache

  dbg_printf("WebM Import - FinishAddingVideoBlocks - numSamples = %ld, numTimes = %ld, numSamplesToAdd=%ld, lastTime_ns = %lld\n", numSamples, numTimes, numSamplesToAdd, lastTime_ns);

  long long blockDuration_ns = 0;
  for (long i = 0; i < numSamplesToAdd; ++i) {
    if (i+1 < numSamples)
      blockDuration_ns = (store->videoTimes[i+1] - store->videoTimes[i]);
    else
      blockDuration_ns = (lastTime_ns - store->videoTimes[i]);    // **** this only works if lastTime_ns is time of next _video_ block (first video block in next cluster)...
    if (blockDuration_ns < 0)
      blockDuration_ns = 0; // catch invalid duration

    if (store->videoTimes[i] > lastTime_ns) {
      dbg_printf("WebM Import - Bogus webm file: Video Block with timestamp > segment duration.\n");
    }
    TimeValue blockDuration_qt = static_cast<TimeValue>(double(blockDuration_ns) / ns_per_sec * mediaTimeScale);
    store->videoSamples[i].durationPerSample = blockDuration_qt;  // TimeValue, count of units, in media's time scale.
    accSectionDuration_qt += blockDuration_qt;    // accumulate duration of section in media time scale

    dbg_printf("\t\t\tV i=%ld, Time=%lld, Duration_ns=%lld, Duration_qt=%ld, mediaTimeScale=%ld\n", i, store->videoTimes[i], blockDuration_ns, store->videoSamples[i].durationPerSample, mediaTimeScale);
    // Alternatively, we can call AddMediaSampleReference() here inside loop, for each block. Supposedly more efficient to call it once after loop, specifying all blocks.
  }

  //
  // Add array of sample references to Media all at once
  //
  dbg_printf("BEFORE AddMediaSampleReferences() the media duration is: %ld\n", GetMediaDuration(store->movieVideoMedia));
  if (numSamplesToAdd > 0) {
    SampleReferencePtr sampleRefs = &store->videoSamples.front(); // &store->videoSamples[0];
    err = AddMediaSampleReferences(store->movieVideoMedia, (SampleDescriptionHandle)store->vp8DescHand, numSamplesToAdd, sampleRefs, NULL);
    if (err) {
      dbg_printf("WebM Import - FinishAddingVideoBlocks - AddMediaSampleReferencs() FAILED with err = %d\n", err);
    }
    store->videoCount += numSamplesToAdd;
  }
  dbg_printf("AFTER AddMediaSampleReferences() the media duration is: %ld\n", GetMediaDuration(store->movieVideoMedia));

  //
  // Insert Media into Track
  //
  long long sectionStart_ns, sectionEnd_ns, sectionDuration_ns = 0;
  TimeValue sectionStart_qt, sectionDuration_qt = 0;

  sectionStart_ns = store->videoTimes[0]; // or if we dont delete the refs, use store->videoTimes[sectionStartIndex];
  sectionStart_qt = static_cast<TimeValue>(double(sectionStart_ns) / ns_per_sec * mediaTimeScale);
  //sectionEnd_ns = lastTime_ns;  // arg passed in
  //sectionDuration_ns = sectionEnd_ns - sectionStart_ns;
  //sectionDuration_qt = static_cast<TimeValue>(double(sectionDuration_ns) / ns_per_sec * mediaTimeScale);
  //if (sectionDuration_qt != accSectionDuration_qt)
  //  dbg_printf("FinishAddingVideoBlocks duration mismatch.  accSecDur_qt=%ld != secDur_qt=%ld\n", accSectionDuration_qt, sectionDuration_qt);
  TimeValue trackStart = -1;  // -1 means insert the media at the END of the track
  TimeValue mediaTime = sectionStart_qt; // starting point of media section to insert, expressed in media's time scale.
  TimeValue mediaDuration = accSectionDuration_qt;  // duration of media section, expressed in media's time scale.
  dbg_printf("videoMaxLoaded=%ld, sectionStart_qt=%ld\n", store->videoMaxLoaded, sectionStart_qt); // test, use maxLoaded in call to Insert instead of calculated value here.
  mediaTime = store->videoMaxLoaded;  // use actual duration value for frames already loaded, rather than calculating it now. Avoid -2014 error from Insert call below.
  err = InsertMediaIntoTrack(store->movieVideoTrack, trackStart, mediaTime, mediaDuration, fixed1);
  if (err)
    dbg_printf("InsertMediaIntoTrack() FAIL with err = %d\n", err);
  dbg_printf("InsertMediaIntoTrack(videoTrack, trackStart=%ld, mediaTime=%ld, mediaDuration=%ld,)\n", trackStart, mediaTime, mediaDuration);
  store->videoMaxLoaded += mediaDuration;

  //
  // Clean up cache
  // Delete sample refs and times after they have been added to quicktime (or maintain ptr into vector)
  //
  store->videoSamples.erase(store->videoSamples.begin(), store->videoSamples.begin()+numSamplesToAdd);  // stl vector.erase(first, last) will not erase last. Range is [first,last).
  store->videoTimes.erase(store->videoTimes.begin(), store->videoTimes.begin()+numSamplesToAdd);

  dbg_printf("FinishAddingVideoBlocks - at end of func, size of video sample vector is: %ld\n", store->videoSamples.size());
  dbg_printf("store->videoTimes[0] = %lld\n", store->videoTimes[0]);
  dbg_printf("store->videoSamples[0].durationPerSample = %ld\n", store->videoSamples[0].durationPerSample);

  return err;
}


//-----------------------------------------------------------------------------
// ShouldAddSamples
// Here we decide if it's OK at the moment to add samples.
//
// Adding sample references to QuickTime movie structures is an
// expensive operation so we don't want to do it too often, especially
// if the movie is currently playing.
//
static Boolean ShouldAddSamples(WebMImportGlobals store) {
  Boolean ret = false;
  unsigned long now = TickCount();
  TimeScale movie_timescale = GetMovieTimeScale(store->movie);
  TimeRecord media_timerecord = {0};

  if (store->movieVideoTrack) {
    media_timerecord.value.lo = store->videoMaxLoaded;
    media_timerecord.scale = GetMediaTimeScale(store->movieVideoMedia);
  } else {
    media_timerecord.value.lo = store->audioMaxLoaded;
    media_timerecord.scale = GetMediaTimeScale(store->movieAudioMedia);
  }

  ConvertTimeScale(&media_timerecord, movie_timescale);

  if ((store->addSamplesLast + kAddSamplesCheckTicks < now) &&
      GetMovieRate(store->movie) != 0) {
    // it's been at least kAddSamplesCheckTicks since we last added samples
    // and the movie is currently playing

    if ((media_timerecord.value.lo - GetMovieTime(store->movie, NULL)) <
        (movie_timescale * kMinPlayheadDistance)) {
      // playhead is near the end of already added data, let's add more
      ret = true;
    }
  } else if (store->addSamplesLast + kAddSamplesNormalTicks < now) {
    // movie is not playing and kAddSamplesNormalTicks passed since
    // last we added - OK to add more
    ret = true;
  }

  return ret;
}


//--------------------------------------------------------------------------------
static const wchar_t* utf8towcs(const char* str)
{
  if (str == NULL)
    return NULL;

  //TODO: this probably requires that the locale be configured somehow

  const size_t size = mbstowcs(NULL, str, 0);

  if (size == 0)
    return NULL;

  wchar_t* const val = new wchar_t[size+1];

  mbstowcs(val, str, size);
  val[size] = L'\0';

  return val;
}


//--------------------------------------------------------------------------------
//  DumpWebMDebugInfo
//  Print debugging info for WebM header, segment, tracks.
//
void DumpWebMDebugInfo(mkvparser::EBMLHeader* ebmlHeader, const mkvparser::SegmentInfo* webmSegmentInfo, const mkvparser::Tracks* webmTracks)
{
  // Header
  dbg_printf("EBMLHeader\n");
  dbg_printf("\tVersion\t\t: %lld\n", ebmlHeader->m_version);
  dbg_printf("\tMaxIDLength\t: %lld\n", ebmlHeader->m_maxIdLength);
  dbg_printf("\tMaxSizeLength\t: %lld\n", ebmlHeader->m_maxSizeLength);
  dbg_printf("\tDocType\t: %s\n", ebmlHeader->m_docType);

  // Segment
  // const wchar_t* const title = utf8towcs(webmSegmentInfo->GetTitleAsUTF8());
  // muxingApp, writingApp
  dbg_printf("Segment Info\n");
  dbg_printf("\tTimeCodeScale\t\t: %lld\n", webmSegmentInfo->GetTimeCodeScale());
  const long long segmentDuration = webmSegmentInfo->GetDuration();
  dbg_printf("\tDuration\t\t: %lld ns\n", segmentDuration);
  const double duration_sec = double(segmentDuration) / ns_per_sec;
  dbg_printf("\tDuration\t\t: %7.3f sec\n", duration_sec);
  //dbg_printf("\tPosition(Segment)\t: %lld\n", webmSegment->m_start); // position of segment payload
  //dbg_printf("\tSize(Segment)\t\t: %lld\n", webmSegment->m_size);  // size of segment payload

  // Tracks
  enum { VIDEO_TRACK = 1, AUDIO_TRACK = 2 };    // track types
  dbg_printf("Tracks\n");
  unsigned long trackIndex = 0;                 // track index is 0-based
  const unsigned long numTracks = webmTracks->GetTracksCount();
  while (trackIndex != numTracks) {
    const mkvparser::Track* const webmTrack = webmTracks->GetTrackByIndex(trackIndex++);
    if (webmTrack == NULL)
      continue;

    // get track info
    unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
    unsigned long trackNum = webmTrack->GetNumber();    // track number is 1-based
    const wchar_t* const trackName = utf8towcs(webmTrack->GetNameAsUTF8());
    const char* const codecID = webmTrack->GetCodecId();
    const wchar_t* const codecName = utf8towcs(webmTrack->GetCodecNameAsUTF8());

    // debug print
    dbg_printf("\tTrack Type\t\t: %ld\n", trackType);
    dbg_printf("\tTrack Number\t\t: %ld\n", trackNum);
    if (codecID != NULL)
      dbg_printf("\tCodec Id\t\t: %s\n", codecID);
    if (codecName != NULL)
      dbg_printf("\tCodec Name\t\t: '%ls'\n", codecName);

    if (trackType == VIDEO_TRACK) {
      const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const mkvparser::VideoTrack* const>(webmTrack);
      long long width = webmVideoTrack->GetWidth();
      long long height = webmVideoTrack->GetHeight();
      double rate = webmVideoTrack->GetFrameRate();
      dbg_printf("\t\twidth = %lld\n", width);
      dbg_printf("\t\theight = %lld\n", height);
      dbg_printf("\t\tframerate = %7.3f\n", rate);
    }

    if (trackType == AUDIO_TRACK) {
      const mkvparser::AudioTrack* const webmAudioTrack = static_cast<const mkvparser::AudioTrack* const>(webmTrack);
      dbg_printf("\t\tAudio Track Sampling Rate: %7.3f\n", webmAudioTrack->GetSamplingRate());
      dbg_printf("\t\tAudio Track Channels: %lld\n", webmAudioTrack->GetChannels());
      dbg_printf("\t\tAudio Track BitDepth: %lld\n", webmAudioTrack->GetBitDepth());

    }
  }  // end track loop

}


//--------------------------------------------------------------------------------
//  DumpWebMGlobals
//  Print debugging info for the QT component globals structure.
//
void DumpWebMGlobals(WebMImportGlobals store)
{
    // DEBUG PRINT THE GLOBALS
  if (store) {
    dbg_printf("WebMImportGlobals:\n");
    dbg_printf("\t self = \t%p\n", store->self);
    dbg_printf("\t webmSegment = \t%p\n", store->webmSegment);
    dbg_printf("\t webmCluster = \t%p\n", store->webmCluster);
    dbg_printf("\t webmTracks = \t%p\n", store->webmTracks);
    dbg_printf("\t segmentDuration = \t%lld\n",store->segmentDuration);

    dbg_printf("\t audioSampleRate = \t%ld\n", store->audioSampleRate);

    dbg_printf("\t fileSize = \t%ld\n", store->fileSize);
    dbg_printf("\t videoSamples size = \t%ld\n", store->videoSamples.size());
    dbg_printf("\t videoTimes size = \t%ld\n", store->videoTimes.size());
    dbg_printf("\t videoMaxLoaded = \t%ld\n", store->videoMaxLoaded);
    dbg_printf("\t audioMaxLoaded = \t%ld\n", store->audioMaxLoaded);


  }
}
