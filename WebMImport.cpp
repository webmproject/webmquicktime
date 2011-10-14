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
  Handle dataRef;
  OSType dataRefType;
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
  // ticks at import start
  unsigned long startTicks;
  // is the component importing in idling mode?
  Boolean usingIdles;
  // placeholder track for visualizing progressive importing
  Track placeholderTrack;
  // amount of bytes successfully parsed so far
  long long parsed_bytes;
  // offset into the current partially parsed cluster
  long long parsed_cluster_offset;
  // timestamp of the first cluster in the file
  long long first_cluster_time_offset;
  // number of media timescale units to subtract from media sample timestamps
  TimeValue video_media_offset;
  TimeValue audio_media_offset;
  // current import process state
  int import_state;
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

// last sample end time not known
static const long kNoEndTime = 0;

enum { VIDEO_TRACK = 1, AUDIO_TRACK = 2 };    // track types

// WebM data parsing status codes.
enum {
  kNoError = 0,
  kNeedMoreData = 1,
  kParseError = 2,
  kEOS = 3,
};

// Importer data processing states.
enum {
  kImportStateParseHeaders = 0,
  kImportStateParseClusters = 1,
  kImportStateFinished = 2,
};

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

static void ResetState(WebMImportGlobals store);
static int ParseDataHeaders(WebMImportGlobals store);
static int ParseDataCluster(WebMImportGlobals store);
OSErr CreateVP8ImageDescription(long width, long height, ImageDescriptionHandle *descOut);
OSErr CreateAudioDescription(SoundDescriptionHandle *descOut, const mkvparser::AudioTrack* webmAudioTrack);
OSErr CreateCookieFromCodecPrivate(const mkvparser::AudioTrack* webmAudioTrack, Handle* cookie);
OSErr AddAudioBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock,
                    long long blockTime_ns);
OSErr AddAudioSampleRefsToQTMedia(WebMImportGlobals store,
                                  long long lastTime_ns);
OSErr AddVideoBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock,
                    long long blockTime_ns);
OSErr AddVideoSampleRefsToQTMedia(WebMImportGlobals store,
                                  long long lastTime_ns);
static int CreateQTTracksAndMedia(WebMImportGlobals store);
static int ProcessCluster(WebMImportGlobals store);
static int ProcessData(WebMImportGlobals store);
static ComponentResult SetupDataHandler(WebMImportGlobals store,
                                        Handle data_ref, OSType data_ref_type);
static ComponentResult InitIdleImport(WebMImportGlobals store);
static ComponentResult NonIdleImport(WebMImportGlobals store);
static Boolean ShouldAddSamples(WebMImportGlobals store);
static ComponentResult CreatePlaceholderTrack(WebMImportGlobals store);
static void RemovePlaceholderTrack(WebMImportGlobals store);
static ComponentResult NotifyMovieChanged(WebMImportGlobals store);
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

  if (store) {
    ResetState(store);
    DisposePtr((Ptr)store);
  }

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


//-----------------------------------------------------------------------------
// WebMImportDataRef
// Parse given file and import it into QuickTime Movie data structures.
//
pascal ComponentResult WebMImportDataRef(WebMImportGlobals store,
                                          Handle dataRef, OSType dataRefType,
                                          Movie theMovie, Track targetTrack,
                                          Track* usedTrack, TimeValue atTime,
                                          TimeValue* durationAdded,
                                          long inFlags, long* outFlags) {
  ComponentResult status = noErr;

  ResetState(store);
  store->movie = theMovie;
  store->loadState = kMovieLoadStateLoading;
  store->import_state = kImportStateParseHeaders;
  *outFlags = 0;
  *durationAdded = 0;

  status = SetupDataHandler(store, dataRef, dataRefType);
  if (status) {
    *outFlags |= movieImportResultComplete;
    return status;
  }

  if (inFlags & movieImportWithIdle) {
    status = InitIdleImport(store);
    if (!status) {
      *outFlags |= movieImportResultNeedIdles;
    } else {
      *outFlags |= movieImportResultComplete;
    }
  } else {
    status = NonIdleImport(store);
    *outFlags |= movieImportResultComplete;

    if (status)
      return status;

    if (store->trackCount > 0) {
      if (durationAdded) {
        const Track track = store->movieVideoTrack ? store->movieVideoTrack :
                      store->movieAudioTrack;
        *durationAdded = GetTrackDuration(track) - atTime;
      }

      if (store->trackCount == 1 && usedTrack) {
        *usedTrack = store->movieVideoTrack ? store->movieVideoTrack :
                     store->movieAudioTrack;
      }
    }
  }

  return status;
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


//-----------------------------------------------------------------------------
// MovieImportValidateDataRef
pascal ComponentResult WebMImportValidateDataRef(WebMImportGlobals store,
                                                 Handle dataRef,
                                                 OSType dataRefType,
                                                 UInt8 *valid) {
  OSErr err = noErr;
  dbg_printf("[WebM Import]  >> [%08lx] :: ValidateDataRef()\n",
             (UInt32) store);
  MkvReaderQT* reader = (MkvReaderQT*) new (std::nothrow) MkvReaderQT;

  if (!reader) {
    err = notEnoughMemoryErr;
  } else {
    // Just open the reader component, don't query the size.
    int status = reader->Open(dataRef, dataRefType, false);
    if (!status) {
      *valid = 128;
      err = noErr;
    } else {
      dbg_printf("[WebM Import] ValidateDataRef() FAIL... err = %d\n", err);
      err = invalidDataRef;
    }
    delete reader;
  }

  dbg_printf("[WebM Import]  << [%08lx] :: ValidateDataRef()\n",
             (UInt32) store);
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


//-----------------------------------------------------------------------------
// WebMImportIdle
// QuickTime will call this repeatedly to give our component some CPU
// time to do a smallish bit of importing, until we indicate we are
// done.
//
pascal ComponentResult WebMImportIdle(WebMImportGlobals store,
                                      long inFlags, long* outFlags) {
  dbg_printf("WebMImportIdle()\n");
  DumpWebMGlobals(store);

  // Give the reader's data handling component some CPU time.
  store->reader->TaskDataHandler();

  if (store->reader->RequestPending()) {
    // Fill buffer request still pending, nothing more to parse at the
    // moment. If the data is coming from network then delay idling by
    // 1/10 second to save CPU time.
    if (store->reader->is_streaming())
      QTIdleManagerSetNextIdleTimeDelta(store->idleManager, 1, 10);
    return noErr;
  }

  int status = 0;
  do {
    status = ProcessData(store);
  } while (!status);

  if (status == kNeedMoreData) {
    status = store->reader->RequestFillBuffer();
    if (!status)
      QTIdleManagerSetNextIdleTimeNow(store->idleManager);
    else if (status == MkvBufferedReaderQT::kFillBufferNotEnoughSpace)
      status = internalComponentErr;
  } else if (status == kEOS) {
    // Skip to finishing-up below.
    status = 0;
  } else if (status == kParseError) {
    *outFlags |= movieImportResultComplete;
    status = invalidDataRef;
  }

  if (status) {
    *outFlags |= movieImportResultComplete;
    return status;
  }

  if (store->import_state == kImportStateFinished) {
    // Add any remaining samples from cache.
    AddVideoSampleRefsToQTMedia(store, store->segmentDuration);
    AddAudioSampleRefsToQTMedia(store, store->segmentDuration);

    RemovePlaceholderTrack(store);

    // set flags to indicate we are done.
    *outFlags |= movieImportResultComplete;
    store->loadState = kMovieLoadStateComplete;
    NotifyMovieChanged(store);

    DumpWebMGlobals(store);
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


//-----------------------------------------------------------------------------
// Estimates the remaining time to fully import the file
// (seems to be needed to properly visualize progressive imports)
//
pascal ComponentResult WebMImportEstimateCompletionTime(
    WebMImportGlobals store, TimeRecord *time) {

  if (time == NULL) {
    return paramErr;
  }

  if (store->segmentDuration <= 0) {
    time->value = SInt64ToWide(S64Set(0));
    time->scale = 0;
  } else {
    TimeScale media_timescale;
    long loaded;
    unsigned long timeUsed = TickCount() - store->startTicks;

    if (store->movieVideoTrack) {
      media_timescale = GetMediaTimeScale(store->movieVideoMedia);
      loaded = store->videoMaxLoaded;
    } else {
      media_timescale = GetMediaTimeScale(store->movieAudioMedia);
      loaded = store->audioMaxLoaded;
    }

    long duration = (double)store->segmentDuration / ns_per_sec *
        media_timescale;
    long estimate = timeUsed * (duration - loaded) / loaded;

    dbg_printf("EstimateCompletionTime(): %ld (%.3f)\n", estimate,
               (double)estimate / 60);

    time->value = SInt64ToWide(S64Set(estimate));
    time->scale = 60;  // roughly TickCount()'s resolution
  }

  time->base = NULL;  // no time base, this time record is a duration
  return noErr;
}


#pragma mark -

//-----------------------------------------------------------------------------
// ResetState
// Bring the component member variables to a clean, initial state,
// releasing any dynamically alocated objects as necessary.
//
static void ResetState(WebMImportGlobals store) {
  store->idleManager = NULL;
  if (store->webmSegment) {
    delete store->webmSegment;
    store->webmSegment = NULL;
  }
  store->movieVideoTrack = NULL;
  store->movieAudioMedia = NULL;
  store->movieVideoMedia = NULL;
  store->movieAudioMedia = NULL;
  store->movie = NULL;
  if (store->vp8DescHand) {
    DisposeHandle((Handle) store->vp8DescHand);
    store->vp8DescHand = NULL;
  }
  if (store->audioDescHand) {
    DisposeHandle((Handle) store->audioDescHand);
    store->audioDescHand = NULL;
  }
  if (store->dataRef) {
    DisposeHandle(store->dataRef);
    store->dataRef = NULL;
  }
  store->dataRefType = 0;
  store->videoCount = 0;
  store->audioCount = 0;
  store->trackCount = 0;
  store->loadState = 0;
  store->videoMaxLoaded = 0;
  store->audioMaxLoaded = 0;
  store->addSamplesLast = 0;
  store->startTicks = 0;
  store->usingIdles = false;
  if (store->placeholderTrack) {
    DisposeMovieTrack(store->placeholderTrack);
    store->placeholderTrack = NULL;
  }
  store->parsed_bytes = 0;
  store->parsed_cluster_offset = 0;
  store->first_cluster_time_offset = -1;
  store->import_state = kImportStateParseHeaders;
  store->video_media_offset = 0;
  store->audio_media_offset = 0;
  if (store->reader) {
    store->reader->Close();
    delete store->reader;
    store->reader = NULL;
  }
}


//-----------------------------------------------------------------------------
// ParseDataHeaders
// Attempt to parse the WebM file header info.
// Will return kNeedMoreData if not enough data is currently
// available, and can be called again when more data becomes
// available. Will set the corresponding component member variables
// when the header structures are successfully parsed.
static int ParseDataHeaders(WebMImportGlobals store) {
  long long parse_status = 0;
  long long pos = 0;

  mkvparser::EBMLHeader ebml_header;
  pos = 0;
  parse_status = ebml_header.Parse(store->reader, pos);
  if (parse_status)
    return kNeedMoreData;

  dbg_printf("ParseDataHeaders: EBMLHeader parsed (pos = %lld)\n", pos);

  mkvparser::Segment* segment;
  parse_status = mkvparser::Segment::CreateInstance(store->reader, pos,                                                    segment);
  if (parse_status)
    return kNeedMoreData;

  if (store->webmSegment)
    delete store->webmSegment;
  store->webmSegment = segment;

  dbg_printf("ParseDataHeaders: segment parsed (pos = %lld)\n", pos);

  parse_status = segment->ParseHeaders();
  if (parse_status)
    return kNeedMoreData;

  dbg_printf("ParseDataHeaders: after ParseHeaders()\n");

  const mkvparser::SegmentInfo* segment_info = segment->GetInfo();
  if (!segment_info)
    return kParseError;

  dbg_printf("ParseDataHeaders: SegmentInfo parsed\n");

  const mkvparser::Tracks* tracks = segment->GetTracks();
  if (!tracks)
    return kParseError;

  dbg_printf("ParseDataHeaders: Tracks parsed\n");

  store->webmTracks = tracks;
  store->segmentDuration = segment_info->GetDuration();
  store->parsed_bytes = tracks->m_element_start + tracks->m_element_size;

  DumpWebMDebugInfo(&ebml_header, segment_info, tracks);
  return kNoError;
}


//-----------------------------------------------------------------------------
// ParseDataCluster
// Attempt to parse one complete WebM cluster, indicating when more
// data is needed or parse errors occured.
//
static int ParseDataCluster(WebMImportGlobals store) {
  int status = 0;
  long size = 0;

  if (!store->webmCluster) {
    long long pos = store->parsed_bytes;
    status = store->webmSegment->LoadCluster(pos, size);
    if (status)
      return kNeedMoreData;
    const mkvparser::Cluster* cluster = store->webmSegment->GetLast();
    if (!cluster || cluster->EOS())
      return kNeedMoreData;
    store->parsed_cluster_offset = pos;
    store->webmCluster = cluster;
  }

  for (;;) {
    status = store->webmCluster->Parse(store->parsed_cluster_offset, size);
    if (status == 1) {
      break;
    } else if (status == mkvparser::E_BUFFER_NOT_FULL) {
      return kNeedMoreData;
    } else if (status != 0) {
      return kParseError;
    }
  }

  const long long element_size = store->webmCluster->GetElementSize();
  store->parsed_bytes = store->webmCluster->m_element_start + element_size;
  store->parsed_cluster_offset = 0;
  dbg_printf("Parsed cluster @ %8lld (size %8lld)\n",
             store->webmCluster->m_element_start, element_size);
  return kNoError;
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
OSErr AddAudioBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns)
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


//-----------------------------------------------------------------------------
// InterpolateDurations
// Divide given duration equally between given samples.
//
// Also, set mediaSampleNotSync on all but the first sample in the
// given range.
// Note, the inclusive [first; last] range of samples to operate on
// should contain at least two elements.
//
static void InterpolateDurations(SampleRefVec &samples, TimeValue duration,
                                 long first, long last) {
  const long inter_duration = duration / (last - first + 1);
  samples[first].durationPerSample = inter_duration;
  for (long i = first + 1; i < last; ++i) {
    samples[i].durationPerSample = inter_duration;
    samples[i].sampleFlags |= mediaSampleNotSync;
  }
  samples[last].durationPerSample = duration - (last - first) * inter_duration;
  samples[last].sampleFlags |= mediaSampleNotSync;
}


//-----------------------------------------------------------------------------
// AddSampleRefsToQTMedia
// Add some sample references to the given QuickTime media and track.
//
// As QuickTime requires the duration of all sample references be
// positive values, here we'll calculate any missing durations (from
// the timestamp of the next-in-queue block or the last_time
// argument).
// The runs of samples with the same timestamp (e.g. multiple frames
// in blocks using lacing) will have durations approximated as if they
// were spread equidistantly between the two samples with known
// different timestamps at the ends of the run; "not sync" flag set on
// all the samples in the run but first to avoid A/V desynchronization
// when seeking.
// A positive value for the argument last_time should be passed if the
// timestamp of the _end_ of the last block (in the samples+times pair
// of vectors) is known, otherwise adding of the last block might be
// deferred until more blocks are available or positive last_time is
// given.
//
static OSErr AddSampleRefsToQTMedia(Media media, Track track, Movie movie,
                                    SampleDescriptionHandle sample_description,
                                    SampleRefVec &samples,
                                    SampleTimeVec &times, long long last_time,
                                    TimeValue * const media_offset,
                                    long * const added_count,
                                    long * const added_duration) {
  OSErr err = noErr;
  TimeValue media_duration = 0, sample_duration = 0;
  const double scaling_factor = (double) GetMediaTimeScale(media) / ns_per_sec;
  long num_samples = samples.size() - 1;
  long last_known_index = -1;

  if (num_samples < 0)
    return err;

  ////
  // Calculate block durations.

  for (long i = 0; i < num_samples; ++i) {
    sample_duration =
        lround(times[i + 1] * scaling_factor) -
        lround(times[i] * scaling_factor);
    if (sample_duration == 0) {
      if (last_known_index == -1)
        last_known_index = i;
    } else if (last_known_index != -1) {
      InterpolateDurations(samples, sample_duration, last_known_index, i);
      media_duration += sample_duration;
      last_known_index = -1;
    } else {
      samples[i].durationPerSample = sample_duration;
      media_duration += sample_duration;
    }
  }

  if (last_time > 0) {
    sample_duration =
        lround(last_time * scaling_factor) -
        lround(times[num_samples] * scaling_factor);
    if (sample_duration > 0) {
      if (last_known_index != -1) {
        InterpolateDurations(samples, sample_duration, last_known_index,
                             num_samples);
        last_known_index = -1;
      } else {
        samples[num_samples].durationPerSample = sample_duration;
      }
      media_duration += sample_duration;
      num_samples += 1;
    } else {
      dbg_printf("AddSampleRefsToQTMedia - invalid block duration at %ld"
                 " (block ts = %lld, last_time = %lld)!?\n",
                 samples[num_samples].dataOffset, times[num_samples],
                 last_time);
    }
  }

  if (last_known_index != -1) {
    // We had some same-timestamp samples at the end of the vector -
    // truncate the range of samples that can be added to QT
    // structures.
    num_samples = last_known_index;
  }

  if (num_samples == 0) {
    return err;
  }

  ////
  // Calculate and apply track offset (on first run).

  if (*added_duration == 0 && times[0] != 0) {
    const TimeValue old_media_start = lround(times[0] * scaling_factor);
    const TimeScale media_scale = GetMediaTimeScale(media);
    const TimeScale movie_scale = GetMovieTimeScale(movie);
    const TimeValue track_offset = old_media_start * movie_scale / media_scale;
    const TimeValue new_media_start =
        lround(track_offset * ((double) media_scale / movie_scale));
    const TimeValue first_media_diff = old_media_start - new_media_start;

    if (track_offset > 0) {
      dbg_printf("AddSampleRefsToQTMedia - track offset = %ld\n",
                 track_offset);
      SetTrackOffset(track, track_offset);
    }

    if (new_media_start > 0) {
      *media_offset = new_media_start;
      dbg_printf("AddSampleRefsToQTMedia - media offset = %ld\n",
                 new_media_start);
    }

    if (first_media_diff > 0) {
      dbg_printf("AddSampleRefsToQTMedia - extending first sample by %ld\n",
                 first_media_diff);
      samples[0].durationPerSample += first_media_diff;
      media_duration += first_media_diff;
      times[0] = new_media_start / scaling_factor;
    }
  }

  ////
  // Insert the num_samples sample references into QT structures.

  err = AddMediaSampleReferences(media, sample_description, num_samples,
                                 &samples.front(), NULL);
  if (err) {
    dbg_printf("AddSampleRefsToQTMedia - AddMediaSampleReferences() FAILED,"
               " err = %d\n", err);
  } else {
    *added_count += num_samples;
    const TimeValue media_start =
        lround(times[0] * scaling_factor) - *media_offset;

    err = InsertMediaIntoTrack(track, -1, media_start, media_duration, fixed1);
    if (err) {
      dbg_printf("AddSampleRefsToQTMedia - InsertMediaIntoTrack() FAILED,"
                 " err = %d\n", err);
    } else {
      *added_duration += media_duration;
    }
  }

  ////
  // Erase consumed samples/times from the queue vectors.

  samples.erase(samples.begin(), samples.begin() + num_samples);
  times.erase(times.begin(), times.begin() + num_samples);

  return err;
}


//-----------------------------------------------------------------------------
// AddAudioSampleRefsToQTMedia
// Insert queued audio sample references into the QuickTime structures.
//
// Also, see the comment at AddSampleRefsToQTMedia().
//
OSErr AddAudioSampleRefsToQTMedia(WebMImportGlobals store,
                                  long long lastTime_ns) {
  return AddSampleRefsToQTMedia(store->movieAudioMedia, store->movieAudioTrack,
                                store->movie,
                                (SampleDescriptionHandle) store->audioDescHand,
                                store->audioSamples, store->audioTimes,
                                lastTime_ns, &store->audio_media_offset,
                                &store->audioCount,
                                &store->audioMaxLoaded);
}


//-----------------------------------------------------------------------------
// AddVideoSampleRefsToQTMedia
// Insert queued video sample references into the QuickTime structures.
//
// Also, see the comment at AddSampleRefsToQTMedia().
//
OSErr AddVideoSampleRefsToQTMedia(WebMImportGlobals store,
                                  long long lastTime_ns) {
  return AddSampleRefsToQTMedia(store->movieVideoMedia, store->movieVideoTrack,
                                store->movie,
                                (SampleDescriptionHandle) store->vp8DescHand,
                                store->videoSamples, store->videoTimes,
                                lastTime_ns, &store->video_media_offset,
                                &store->videoCount,
                                &store->videoMaxLoaded);
}


//--------------------------------------------------------------------------------
//  AddVideoBlock
//  Add one video block to cache of sample references.
//  Call this for each block in a section (maybe a cluster) to collect sample references first,
//  and then call AddMediaSampleReferences() later for the section, or once at the end of file.
//
//  Lacing means there could be multiple Frames per Block, so iterate and add all Frames here.
//
OSErr AddVideoBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns)
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


//-----------------------------------------------------------------------------
// CreateQTTracksAndMedia
// Create QuickTime sample descriptions, media and tracks according to
// what we have found in the WebM file.
//
static int CreateQTTracksAndMedia(WebMImportGlobals store) {
  const unsigned long num_tracks = store->webmTracks->GetTracksCount();
  OSErr err = noErr;

  for (unsigned long i = 0; i < num_tracks; ++i) {
    const mkvparser::Track* track = store->webmTracks->GetTrackByIndex(i);
    if (track == NULL)
      continue;

    const long long trackType = track->GetType();
    if (trackType == VIDEO_TRACK) {
      const mkvparser::VideoTrack* const video_track =
          static_cast<const mkvparser::VideoTrack* const>(track);
      long long width = video_track->GetWidth();
      long long height = video_track->GetHeight();

      // Create image description so QT can find appropriate decoder
      // component (our VP8 decoder).
      if (store->vp8DescHand == NULL) {
        err = CreateVP8ImageDescription(width, height, &store->vp8DescHand);
        if (err)
          return err;
      }

      if (store->movieVideoTrack == NULL) {
        // Passing width and height as 32bit fixed floats (16bits + 16bits).
        store->movieVideoTrack = NewMovieTrack(store->movie, width << 16,
                                               height << 16, kNoVolume);
        store->trackCount++;

        // By default QT uses 600 units per second as a movie
        // timescale, should be enough for video, too.
        TimeScale timeScale = GetMovieTimeScale(store->movie);
        store->movieVideoMedia = NewTrackMedia(store->movieVideoTrack,
                                               VideoMediaType, timeScale,
                                               store->dataRef,
                                               store->dataRefType);
        if (err = GetMoviesError())
          return err;

        dbg_printf("timeScale passed to NewTrackMedia is: %ld\n", timeScale);
        SetTrackEnabled(store->movieVideoTrack, true);
      }
    } else if (trackType == AUDIO_TRACK) {
      const mkvparser::AudioTrack* const audio_track =
          static_cast<const mkvparser::AudioTrack* const>(track);

      // Create sound description so QT can find appropriate codec
      // component for decoding the audio data.
      if (store->audioDescHand == NULL) {
        err = CreateAudioDescription(&store->audioDescHand, audio_track);
        if (err) {
          dbg_printf("WebM Import - CreateAudioDescription() Failed"
                     " with %d.\n", err);
          return err;
        }
      }

      if (store->movieAudioTrack == NULL) {
        // Passing 0 for width and height of audio track.
        store->movieAudioTrack = NewMovieTrack(store->movie, 0, 0, kFullVolume);
        store->trackCount++;

        long sample_rate = audio_track->GetSamplingRate();
        store->movieAudioMedia = NewTrackMedia(store->movieAudioTrack,
                                               SoundMediaType, sample_rate,
                                               store->dataRef,
                                               store->dataRefType);
        if (err = GetMoviesError())
          return err;

        SetTrackEnabled(store->movieAudioTrack, true);
      }
    }
  }

  return 0;
}


//-----------------------------------------------------------------------------
// ProcessCluster
// One WebM cluster is ready so process it: extract all the blocks,
// add them to component's video and audio queues and possibly flush
// the queues if the time is right.
//
static int ProcessCluster(WebMImportGlobals store) {
  int status = 0;
  const mkvparser::BlockEntry* block_entry = store->webmCluster->GetFirst();

  if (store->first_cluster_time_offset == -1) {
    store->first_cluster_time_offset = store->webmCluster->GetTime();
  }

  while ((block_entry != NULL) && (!block_entry->EOS())) {
    const mkvparser::Block* const block = block_entry->GetBlock();
    const long long block_time = block->GetTime(store->webmCluster);

    const mkvparser::Track* track =
        store->webmTracks->GetTrackByNumber(block->GetTrackNumber());
    const long long track_type = track->GetType();

    if (track_type == VIDEO_TRACK) {
      status = AddVideoBlock(store, block,
                             block_time - store->first_cluster_time_offset);
    } else if (track_type == AUDIO_TRACK) {
      status = AddAudioBlock(store, block,
                             block_time - store->first_cluster_time_offset);
    }

    if (status)
      return status;

    block_entry = store->webmCluster->GetNext(block_entry);
  }

  store->webmCluster = NULL;

  if (!store->usingIdles || ShouldAddSamples(store)) {
    AddVideoSampleRefsToQTMedia(store, kNoEndTime);
    AddAudioSampleRefsToQTMedia(store, kNoEndTime);
    store->addSamplesLast = TickCount();
    if (store->usingIdles) {
      store->loadState = kMovieLoadStatePlayable;
      NotifyMovieChanged(store);
    }
  }

  return 0;
}


//-----------------------------------------------------------------------------
// ProcessData
// The component's main import state-dispatch-loop-step processing function.
//
static int ProcessData(WebMImportGlobals store) {
  int status = 0;

  dbg_printf("[WebM Import]  >> [%p] :: ProcessData()\n", store);
  switch (store->import_state) {
    case kImportStateParseHeaders:
      status = ParseDataHeaders(store);
      dbg_printf("ParseDataHeaders() = %d\n", status);
      if (status)
        return status;
      status = CreateQTTracksAndMedia(store);
      dbg_printf("CreateQTTracksAndMedia() = %d\n", status);
      if (status || store->trackCount == 0) {
        store->import_state = kImportStateFinished;
        return status;
      }
      store->import_state = kImportStateParseClusters;
      if (store->usingIdles)
        CreatePlaceholderTrack(store);
      break;

    case kImportStateParseClusters:
      status = ParseDataCluster(store);
      dbg_printf("ParseDataCluster() = %d (EOS: %d)\n", status,
                 store->reader->eos());
      if (status == kNeedMoreData && store->reader->eos()) {
        store->import_state = kImportStateFinished;
        return kEOS;
      }
      if (status)
        return status;
      status = ProcessCluster(store);
      dbg_printf("ProcessCluster() = %d\n", status);
      break;

    default:
      // do nothing
      break;
  };

  return status;
}


//-----------------------------------------------------------------------------
// SetupDataHandler
// Open and configure the data reader.
//
static ComponentResult SetupDataHandler(WebMImportGlobals store,
                                        Handle data_ref,
                                        OSType data_ref_type) {
  // Copy the data reference handle so we can use it after the
  // original import call has finished.
  int status = HandToHand(&data_ref);
  if (status)
    return status;

  store->dataRef = data_ref;
  store->dataRefType = data_ref_type;

  MkvBufferedReaderQT* reader = new (std::nothrow) MkvBufferedReaderQT();
  if (!reader)
    return notEnoughMemoryErr;

  status = reader->Open(data_ref, data_ref_type);
  if (status) {
    dbg_printf("[WebM Import] MkvReaderQT::Open() Error, status = %d\n",
               status);
    delete reader;
    return status;
  }

  // If data is not coming from network, to improve performance set a
  // bigger read chunk size.
  if (!reader->is_streaming())
    reader->set_chunk_size(MkvBufferedReaderQT::kMaxReadChunkSize);

  store->reader = reader;
  return noErr;
}


//-----------------------------------------------------------------------------
// InitIdleImport
// Initialize import-with-idles process by asking the reader to fetch
// some data.
//
static ComponentResult InitIdleImport(WebMImportGlobals store) {
  store->usingIdles = true;
  store->startTicks = TickCount();

  int status = store->reader->RequestFillBuffer();
  if (status == MkvBufferedReaderQT::kFillBufferNotEnoughSpace) {
    // Should never happen.
    return internalComponentErr;
  }
  return noErr;
}


//-----------------------------------------------------------------------------
// NonIdleImport
// Import the whole WebM file in one go.
// It uses the same machanics as idle importing but the loop is done
// by us instead of QuickTime.
//
static ComponentResult NonIdleImport(WebMImportGlobals store) {
  int status = 0;

  while (true) {
    do {
      status = ProcessData(store);
    } while (!status);

    if (status == kNeedMoreData) {
      status = store->reader->RequestFillBuffer();
      if (status == MkvBufferedReaderQT::kFillBufferNotEnoughSpace) {
        // We won't be consuming/freeing any more buffered data for
        // now, so we can only give up.
        return internalComponentErr;
      } else if (status) {
        return status;
      }
      while (store->reader->RequestPending()) {
        store->reader->TaskDataHandler();
      }
    } else if (status == kEOS) {
      break;
    } else if (status == kParseError) {
      return invalidDataRef;
    } else {
      return status;
    }
  }

  if (store->import_state != kImportStateFinished) {
    return internalComponentErr;
  }

  // Add any remaining samples from cache.
  AddVideoSampleRefsToQTMedia(store, store->segmentDuration);
  AddAudioSampleRefsToQTMedia(store, store->segmentDuration);

  NotifyMovieChanged(store);

  DumpWebMGlobals(store);
  return noErr;
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


//-----------------------------------------------------------------------------
// CreatePlaceholderTrack
// Create and add to the movie a disabled track containing a single
// sample reference the duration of the movie length. In our component
// the movie length estimate is the segment duration if defined,
// otherwise no placeholder track is created.
//
static ComponentResult CreatePlaceholderTrack(WebMImportGlobals store) {
  ComponentResult err = noErr;
  Track track = NULL;
  Media media = NULL;
  SampleDescriptionHandle sample_desc;

  if (store->segmentDuration <= 0) {
    // no placeholder tracks for live streams
    return err;
  }

  sample_desc = (SampleDescriptionHandle)
      NewHandleClear(sizeof(SampleDescription));

  if (sample_desc == NULL)
    err = MemError();

  if (!err) {
    (*sample_desc)->descSize = sizeof(SampleDescriptionPtr);
    (*sample_desc)->dataFormat = BaseMediaType;

    track = NewMovieTrack(store->movie, 0, 0, 0);  // dimensionless and mute
    if (track != NULL) {
      TimeScale movie_timescale = GetMovieTimeScale(store->movie);
      long duration = (double)store->segmentDuration / ns_per_sec *
          movie_timescale;

      SetTrackEnabled(track, false);
      media = NewTrackMedia(track, BaseMediaType, movie_timescale,
                            NewHandle(0), NullDataHandlerSubType);

      if (media != NULL) {
        // add a single sample, length of "duration"
        err = AddMediaSampleReference(media, 0, 1, duration, sample_desc,
                                      1, 0, NULL);

        if (!err)
          err = InsertMediaIntoTrack(track, 0, 0, duration, fixed1);
        if (!err) {
          if (store->placeholderTrack != NULL)
            DisposeMovieTrack(store->placeholderTrack);
          store->placeholderTrack = track;
        } else {
          DisposeMovieTrack(track);
        }
      } else {
        err = GetMoviesError();
        DisposeMovieTrack(track);
      }
    } else {
      err = GetMoviesError();
    }

    DisposeHandle((Handle) sample_desc);
  }

  return err;
}


//-----------------------------------------------------------------------------
// RemovePlaceholderTrack
// Remove the placeholder track, if it exists.
//
static void RemovePlaceholderTrack(WebMImportGlobals store) {
  if (store->placeholderTrack) {
    DisposeMovieTrack(store->placeholderTrack);
    store->placeholderTrack = NULL;
  }
}


//-----------------------------------------------------------------------------
// NotifyMovieChanged
// Make some QuickTime voodoo to make sure it notices all the changes
// we've made to the movie structures. Seems necessary for live
// streams and some short files (?!).
//
static ComponentResult NotifyMovieChanged(WebMImportGlobals store) {
  QTAtomContainer container = NULL;
  ComponentResult err = QTNewAtomContainer(&container);

  if (!err) {
    QTAtom action;
    OSType action_type = EndianU32_NtoB(kActionMovieChanged);

    err = QTInsertChild(container, kParentAtomIsContainer, kAction,
                        1, 0, 0, NULL, &action);
    if (!err)
      err = QTInsertChild(container, action, kWhichAction, 1, 0,
                          sizeof(action_type), &action_type, NULL);
    if (!err)
      err = MovieExecuteWiredActions(store->movie, 0, container);

    dbg_printf("NotifyMovieChanged() = %ld\n", (long)err);
    err = QTDisposeAtomContainer(container);
  }

  return err;
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
    dbg_printf("\t videoSamples size = \t%ld\n", store->videoSamples.size());
    dbg_printf("\t videoTimes size = \t%ld\n", store->videoTimes.size());
    dbg_printf("\t videoMaxLoaded = \t%ld\n", store->videoMaxLoaded);
    dbg_printf("\t audioMaxLoaded = \t%ld\n", store->audioMaxLoaded);
    dbg_printf("\t import_state = \t%d\n", store->import_state);
    dbg_printf("\t parsed_bytes = \t%lld\n", store->parsed_bytes);
    dbg_printf("\t cluster_offset = \t%lld\n", store->parsed_cluster_offset);
  }
}
