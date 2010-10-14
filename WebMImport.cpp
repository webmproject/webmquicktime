// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

//
// Quicktime Movie Import Commponent for the WebM format.
// See www.webmproject.org for more info.
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


//typedef std::multimap<long, SampleReferencePtr> SampleRefMap;
typedef std::vector<SampleReferenceRecord> SampleRefVec;
typedef std::vector<long long> SampleTimeVec;

#define USE_SAMPLE_MAP 1
  
// WebM Import Component Globals structure
typedef struct {
  ComponentInstance self;
  // Boolean idlingImporter;
  ::Track movieVideoTrack, movieAudioTrack;
  ::Media movieVideoMedia, movieAudioMedia;
  ::Movie movie;
  ImageDescriptionHandle videoDescHand;
  SoundDescriptionHandle audioDescHand;
  Handle dataRef;
  OSType dataRefType;
  long  dataHOffset;
  long  fileSize;
  ComponentInstance dataHandler;
  //SampleRefVec videoSamples;
  SampleRefVec audioSamples;
  SampleTimeVec audioTimes;
} WebMImportGlobalsRec, *WebMImportGlobals;

static const long long ns_per_sec = 1000000000;

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
OSErr AddAudioBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns, const mkvparser::AudioTrack* webmAudioTrack);
OSErr FinishAddingAudioBlocks(WebMImportGlobals store, long long lastTime_ns);
void DumpWebMDebugInfo(mkvparser::EBMLHeader* ebmlHeader, const mkvparser::SegmentInfo* webmSegmentInfo, const mkvparser::Tracks* webmTracks);


//--------------------------------------------------------------------------------
static const wchar_t* utf8towcs(const char* str)
{
  if (str == NULL)
    return NULL;
  
  //TODO: this probably requires that the locale be
  //configured somehow:
  
  const size_t size = mbstowcs(NULL, str, 0);
  
  if (size == 0)
    return NULL;
  
  wchar_t* const val = new wchar_t[size+1];
  
  mbstowcs(val, str, size);
  val[size] = L'\0';
  
  return val;
}

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


#pragma mark-
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
//
pascal ComponentResult WebMImportDataRef(WebMImportGlobals store, Handle dataRef, OSType dataRefType,
                                         Movie theMovie, Track targetTrack, Track *usedTrack,
                                         TimeValue atTime, TimeValue *durationAdded, 
                                         long inFlags, long *outFlags)
{
  OSErr err = noErr;
  store->movie = theMovie;
  store->dataRef = dataRef;
  store->dataRefType = dataRefType;
  ::Track movieVideoTrack = NULL;
  Media movieVideoMedia;
  ImageDescriptionHandle vp8DescHand = NULL;
  ComponentInstance dataHandler = 0;
  const long long ns_per_sec = 1000000000;  // conversion factor, ns to sec

  store->audioDescHand = NULL; // leak?
  
  dbg_printf("[WebM Import]  >> [%08lx] :: FromDataRef(dataRef = %08lx, atTime = %ld)\n", (UInt32) store, (UInt32) dataRef, atTime); // targetTrack != NULL
  
  // The movieImportMustUseTrack flag indicates that we must use an existing track.
	// We don't support this and always create a new track, so return paramErr.
	if (inFlags & movieImportMustUseTrack)
		return paramErr;

  // Use IMkvReader subclass that knows about quicktime dataRef and dataHandler objects, rather than plain file io.
  long long pos = 0;
  MkvReaderQT reader;
  int status = reader.Open(dataRef, dataRefType);
  if (status != 0) {
    dbg_printf("[WebM Import] MkvReaderQT::Open() Error, status = %d\n", status);
    return -1;
  }
  
  long long totalLength = 0;
  long long availLength = 0;
  reader.Length(&totalLength, &availLength);
  dbg_printf("MkvReaderQT.m_length = %lld bytes. availableLength = %lld\n", totalLength, availLength);
  
  // Use the libwebm project (libmkvparser.a) to parse the WebM file.

  //
  // WebM Header
  //
  using namespace mkvparser;
  EBMLHeader ebmlHeader;
  long long headerstatus = ebmlHeader.Parse(&reader, pos);
  if (headerstatus != 0) {
    dbg_printf("[WebM Import] EBMLHeader.Parse() Error, returned %lld.\n", headerstatus );
    return -1;  // or maybe invalidDataRef
  }

  //
  // WebM Segment
  //
  mkvparser::Segment* webmSegment;
  long long ret = mkvparser::Segment::CreateInstance(&reader, pos, webmSegment);
  if (ret) {
    dbg_printf("Segment::CreateInstance() failed.\n");
    return -1;
  }

  ret = webmSegment->Load();
  if (ret) {
    dbg_printf("Segment::Load() failed.\n");
    return -1;
  }
  
  //
  // WebM SegmentInfo
  //
  const SegmentInfo* const webmSegmentInfo = webmSegment->GetInfo();
  const long long segmentDuration = webmSegmentInfo->GetDuration();
  
  //
  // WebM Tracks
  //
  mkvparser::Tracks* const webmTracks = webmSegment->GetTracks();
  enum { VIDEO_TRACK = 1, AUDIO_TRACK = 2 };    // track types

  // Print debug info on the WebM header, segment, and tracks information.
  DumpWebMDebugInfo(&ebmlHeader, webmSegmentInfo, webmTracks);
  
  const unsigned long clusterCount = webmSegment->GetCount();
  if (clusterCount == 0) {
    dbg_printf("Segment has no Clusters!\n");
    delete webmSegment;
    return -1;
  }
  dbg_printf("Cluster Count\t\t: %ld\n", clusterCount);
  
  // Remember previous block.
  int prevBlock = 0;
  long long prevBlockOffset = 0;
  long long prevBlockSize = 0;
  long long prevBlockTime = 0;
  
  //
  //  WebM Cluster
  //
  mkvparser::Cluster* webmCluster = webmSegment->GetFirst();
  while ((webmCluster != NULL) && !webmCluster->EOS()) 
  {
    const long long timeCode = webmCluster->GetTimeCode();
    const long long time_ns = webmCluster->GetTime();
    dbg_printf("TIME - Cluster Time: %lld\n", time_ns); // ****
    
    //
    //  WebM Block
    //
    const BlockEntry* webmBlockEntry = webmCluster->GetFirst();
    while ((webmBlockEntry != NULL) && (!webmBlockEntry->EOS()))
    {
      const mkvparser::Block* const webmBlock = webmBlockEntry->GetBlock();

      const unsigned long trackNum = webmBlock->GetTrackNumber(); // block's track number (see 
      const mkvparser::Track* webmTrack = webmTracks->GetTrackByNumber(trackNum);
      const unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
      const long blockSize = webmBlock->GetSize();  // webmBlock->m_size;
      const long long blockTime_ns = webmBlock->GetTime(webmCluster);
      // dbg_printf("libwebm block->GetSize=%ld, block->m_size=%lld\n", webmBlock->GetSize(), webmBlock->m_size);   // GetSize() is size of video data, m_size is size of WebM block (includes some header)
      // dbg_printf("libwebm block->GetOffset=%ld, block->m_start=%lld\n", webmBlock->GetOffset(), webmBlock->m_start);
      // dbg_printf("TIME - Block Time: %lld\n", blockTime_ns);
      dbg_printf("BLOCK\t\t:%s,%15ld,%s,%15lld\n", (trackType == VIDEO_TRACK) ? "V" : "A", blockSize, webmBlock->IsKey() ? "I" : "P", blockTime_ns);
      
      if (trackType == VIDEO_TRACK) {
        //
        // WebM Video Data
        //
        const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const VideoTrack* const>(webmTrack);
        long long width = webmVideoTrack->GetWidth();
        long long height = webmVideoTrack->GetHeight();
        
#pragma mark QTStuff
        //
        // QuickTime movie stuff begins here...
        //
        // NewMovieTrack()
        // NewTrackMedia()
        // SetTrackEnabled()
        // while {
        //    AddMediaSampleReference()
        //    incr to next frame
        // InsertMediaIntoTrack()
        // GetTrackDuration()

        // Create image description so QT can find appropriate decoder component (our VP8 decoder)
        if (vp8DescHand == NULL) {
          err = CreateVP8ImageDescription(width, height, &vp8DescHand);
          if (err) goto bail;
        }
        // Create QT movie track and media (not WebM file track)
        if (movieVideoTrack == NULL) {

          // Create a new QT video track
          movieVideoTrack = NewMovieTrack(theMovie, (**vp8DescHand).width << 16, (**vp8DescHand).height << 16, kNoVolume);
          
          // Create a new QT media for the track
          // (The media refers to the actual data samples used by the track.)
          TimeScale timeScale = GetMovieTimeScale(theMovie);  // QT uses 600 units per second
          movieVideoMedia = NewTrackMedia(movieVideoTrack, VideoMediaType, timeScale, dataRef, dataRefType);
          if (err = GetMoviesError()) goto bail;
          dbg_printf("timeScale passed to NewTrackMedia is: %ld\n", timeScale);
          
          // Enable the track.
          SetTrackEnabled(movieVideoTrack, true);
        } // end quicktime movie track
 
        // If we have a previous block (i.e. this is not the first block in the file), 
        // then calculate duration and add the previous block.
        // (We can't look ahead to next block because it may be in a different cluster.)
        if (prevBlock != 0) {
          // Calculate duration of previous block
          long long blockDuration_ns = (blockTime_ns - prevBlockTime);    // ns, or (nextFrameTime - frameTime)
          // Convert from ns to QT time base units
          TimeValue frameDuration = static_cast<TimeValue>(double(blockDuration_ns) / ns_per_sec * GetMovieTimeScale(theMovie));
          dbg_printf("TIME - block duration (ns)\t: %ld\n", blockDuration_ns);
          dbg_printf("TIME - QT frame duration\t: %ld\n", frameDuration);
          
          // Add the media sample
          // AddMediaSampleReference does not add sample data to the file or device that contains a media.
          // Rather, it defines references to sample data contained elswhere. Note that one reference may refer to more
          // than one sample--all the samples described by a reference must be the same size. This function does not update the movie data
          // as part of the add operation therefore you do not have to call BeginMediaEdits before calling AddMediaSampleReference.          
          err = AddMediaSampleReference(movieVideoMedia, 
                                        prevBlockOffset,                          // offset into the data file
                                        prevBlockSize,                            // number of bytes of sample data to be identified by ref
                                        frameDuration,                            // duration of each sample in the reference (calculated duration of prevBlock)
                                        (SampleDescriptionHandle)vp8DescHand,     // Handle to a sample description
                                        1,                                        // number of samples contained in the reference
                                        0,                                        // flags
                                        NULL);                                    // returns time where reference was inserted, NULL to ignore
          if (err) goto bail;
          dbg_printf("AddMediaSampleReference(offset=%lld, size=%lld, duration=%ld)\n", prevBlockOffset, prevBlockSize, frameDuration);
        }

        // save current block info for next iteration
        prevBlock = 1;
        prevBlockOffset = webmBlock->GetOffset();   // Block::GetOffset() is offset to vp8 frame data, Block::m_start is offset to MKV Block.
        prevBlockSize = webmBlock->GetSize();       // note: webmBlock->m_size is 4 bytes less than webmBlock->GetSize()
        prevBlockTime = blockTime_ns;
        
      } // end video track
      else if (trackType == AUDIO_TRACK) {
        //
        // WebM Audio Data
        //
        const mkvparser::AudioTrack* const webmAudioTrack = static_cast<const mkvparser::AudioTrack* const>(webmTrack);
        err = AddAudioBlock(store, webmBlock, blockTime_ns, webmAudioTrack);
        if (err) goto bail;
      }
      
      // Advance to next webm Block within this Cluster.
      webmBlockEntry = webmCluster->GetNext(webmBlockEntry);
    } // end block loop
    
    // Advance to next webm Cluster
    webmCluster = webmSegment->GetNext(webmCluster);
  } // end cluster loop
 
  // Add the last video block in the file now, using the "prevBlock" fields stored above. 
  if (prevBlock != 0) {
    // Calculate duration of last block by subtracting time from duration of entire segment
    long long blockDuration_ns = (segmentDuration - prevBlockTime);    // ns, or (nextFrameTime - frameTime)
    // Convert from ns to QT time base units
    TimeValue frameDuration = static_cast<TimeValue>(double(blockDuration_ns) / ns_per_sec * GetMovieTimeScale(theMovie));
    dbg_printf("TIME - block duration: %ld\n", blockDuration_ns);
    dbg_printf("TIME - QT frame duration: %ld\n", frameDuration);
    err = AddMediaSampleReference(movieVideoMedia, 
                                  prevBlockOffset,                          // offset into the data file
                                  prevBlockSize,                            // number of bytes of sample data to be identified by ref
                                  frameDuration,                            // duration of each sample in the reference (calculated duration of prevBlock)
                                  (SampleDescriptionHandle)vp8DescHand,     // Handle to a sample description
                                  1,                                        // number of samples contained in the reference
                                  0,                                        // flags
                                  NULL);                                    // returns time where reference was inserted, NULL to ignore
    if (err) goto bail;
  }
  
  //
  // Add audio samples to media
  //
  FinishAddingAudioBlocks(store, segmentDuration);   // duration = webmSegmentInfo->GetDuration();
  
  // Insert the added media into the track
  // Time value specifying where the segment is to be inserted in the movie's time scale, -1 to add the media data to the end of the track
  TimeValue mediaDuration = GetMediaDuration(movieVideoMedia);
  err = InsertMediaIntoTrack(movieVideoTrack, atTime, 0, mediaDuration, fixed1);                // VIDEO
  if (err) goto bail;
  TimeValue audioMediaDuration = GetMediaDuration(store->movieAudioMedia);
  err = InsertMediaIntoTrack(store->movieAudioTrack, atTime, 0, audioMediaDuration, fixed1);    // AUDIO
  if (err) goto bail;
  
  // Return the duration added 
  if (movieVideoTrack != NULL)
    *durationAdded = GetTrackDuration(movieVideoTrack) - atTime;
  
bail:
  if (err)
    dbg_printf("[WebM Import] - BAIL FAIL !!! ", err);
	if (movieVideoTrack) { // QT videoTrack
		if (err) {
			DisposeMovieTrack(movieVideoTrack);
			movieVideoTrack = NULL;
		} else {
			// Set the outFlags to reflect what was done.
			*outFlags |= movieImportCreateTrack;
		}
	}
  if (vp8DescHand) {
    DisposeHandle((Handle)vp8DescHand);
    vp8DescHand = NULL;
  }
  
	// Remember to close what you open.
	if (dataHandler)
		CloseComponent(dataHandler);
  
	// Return the track identifier of the track that received the imported data in the usedTrack pointer. Your component
	// needs to set this parameter only if you operate on a single track or if you create a new track. If you modify more
	// than one track, leave the field referred to by this parameter unchanged. 
	//if (usedTrack) *usedTrack = movieVideoTrack;

  dbg_printf("[WebM Import]  << [%08lx] :: FromDataRef(%d, %ld)\n", (UInt32) store, targetTrack != NULL, atTime);

	return err;  
}


//--------------------------------------------------------------------------------
// MovieImportGetMimeTypeList
pascal ComponentResult WebMImportGetMIMETypeList(WebMImportGlobals store, QTAtomContainer *outMimeInfo)
{
  dbg_printf("[WebM Import]  >> [%08lx] :: GetMIMETypeList()\n", (UInt32) store);
  dbg_printf("[WebM Import]  << [%08lx] :: GetMIMETypeList()\n", (UInt32) store);
  return GetComponentResource((Component)store->self, 'mime', 263, (Handle *)outMimeInfo);
}


// MovieImportValidate
// MovieImportValidateDataRef
// MovieImportRegister


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
  descPtr->hRes = 72L << 16;                      // horizontal resolution dpi ****
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

  // In all fields, a value of 0 indicates that the field is either unknown, not applicable or otherwise is inapproprate for the format and should be ignored.    
	AudioStreamBasicDescription asbd;
  asbd.mFormatID = kAudioFormatVorbis;  // kAudioFormatLinearPCM;
  asbd.mSampleRate = webmAudioTrack->GetSamplingRate();       // 48000.; // or whatever your sample rate is
  //asbd.mFormatFlags = // kAudioFormatFlagIsBigEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked; // or leave off big endian if it's LittleEndian
  asbd.mChannelsPerFrame = webmAudioTrack->GetChannels();     // 1;
  asbd.mBitsPerChannel = 0;       // 24; // or webmAudioTrack->GetBitDepth() ? ****
  asbd.mFramesPerPacket = 1;      // ****
  asbd.mBytesPerFrame = 0;        // 3;
  asbd.mBytesPerPacket = 0;       // 3;
  
  dbg_printf("WebMImport CreateAudioDescription() - asbd.mSampleRate = %7.3f\n", asbd.mSampleRate);
  err = QTSoundDescriptionCreate(&asbd,     // description of the format
                                 NULL,      // AudioChannelLayout, NULL if there isn't one
                                 0,         // ByteCount, size of audio channel layout, (0 if above is NULL)
                                 NULL,      // magic cookie?
                                 0,         // magic cookie size?
                                 kQTSoundDescriptionKind_Movie_LowestPossibleVersion,   // QTSoundDescriptionKind
                                 &descHand);     // out, sound description
  if (err == noErr) {
    *descOut = descHand;
  }
  dbg_printf("WebMImport CreateAudioDescription() - descHand.sampleRate = %7.3f\n", (*descHand)->sampleRate);
  return err;
}


//--------------------------------------------------------------------------------
//  AddAudioBlock()
//  Add an audio block reference to list. 
//
OSErr AddAudioBlock(WebMImportGlobals store, const mkvparser::Block* webmBlock, long long blockTime_ns, const mkvparser::AudioTrack* webmAudioTrack)
{
  OSErr err = noErr;

  //const mkvparser::AudioTrack* const webmAudioTrack = static_cast<const mkvparser::AudioTrack* const>(webmTrack);
  //dbg_printf("\t\tAudio Track Sampling Rate: %7.3f\n", webmAudioTrack->GetSamplingRate());
  //dbg_printf("\t\tAudio Track Channels: %d\n", webmAudioTrack->GetChannels());
  //dbg_printf("\t\tAudio Track BitDepth: %lld\n", webmAudioTrack->GetBitDepth());

  // Create sound description so QT can find appropriate codec component for decoding the audio data.
  if (store->audioDescHand == NULL) {
    err = CreateAudioDescription(&store->audioDescHand, webmAudioTrack);
    if (err) {
      dbg_printf("WebM Import - CreateAudioDescription() Failed with %d.\n", err);
      goto bail;
    }
  }
  
  // Create a QT audio track object, a QT media object, and then enable the track.
  if (store->movieAudioTrack == NULL) {
    // Create a new QT video track
    store->movieAudioTrack = NewMovieTrack(store->movie, 0, 0, kFullVolume); // pass 0 for width and height of audio track.
    if (store->movieAudioTrack == NULL) goto bail;
  
    // Create a new QT media for the track    
    long sampleRate = 0;
    sampleRate = GetMovieTimeScale(store->movie);
    sampleRate = webmAudioTrack->GetSamplingRate();
    store->movieAudioMedia = NewTrackMedia(store->movieAudioTrack, SoundMediaType, sampleRate, store->dataRef, store->dataRefType);
    if (err = GetMoviesError()) goto bail;

    // Enable the track.
    SetTrackEnabled(store->movieAudioTrack, true);    
  }

#if USE_SAMPLE_MAP
  // **** Collect sample references first, and then call AddMediaSampleReferences() once at the end.
  SampleReferencePtr srp;
  srp = (SampleReferencePtr)malloc(sizeof(SampleReferenceRecord));
  srp->dataOffset = webmBlock->GetOffset();
  srp->dataSize = webmBlock->GetSize();
  srp->durationPerSample = 0;               // need to calculate this later for all samples.
  srp->numberOfSamples = 1;
  srp->sampleFlags = 0; // ?
  // calculate duration of previous sample (tail of list), set duration, add new sample
  // Or just add it now, then later loop through and calc duration of each one on the fly (next->time - this->time)
  
  //store->audioSamples.insert(make_pair(t, srp));  // alternatively, use stl multimap to ensure sort order by time.  Not contiguous though, so can't add all samples at once.
  store->audioSamples.push_back(*srp);
  store->audioTimes.push_back(blockTime_ns);  // **** needs to be passed into AddAudioBlock(), or pass in webmCluster so we can call webmBlock->GetTime(webmCluster); here.
  
  dbg_printf("Audio Block: (offset=%ld, size=%ld, duration=calculated later.\n", srp->dataOffset, srp->dataSize);
  
#else  
  // Add the media sample
  err = AddMediaSampleReference(store->movieAudioMedia, webmBlock->GetOffset(), webmBlock->GetSize(), 0, (SampleDescriptionHandle)store->audioDescHand, 1, 0, NULL);
  if (err) goto bail;
  dbg_printf("AUDIO AddMediaSampleReference(offset=%lld, size=%lld, duration=%ld)\n", webmBlock->GetOffset(), webmBlock->GetSize(), 0);
#endif
  
  return noErr;

bail:
  dbg_printf("WebM Import - AddAudioBlock() Failed. \n");
  return err;
}


//--------------------------------------------------------------------------------
//  FinishAddingAudioBlocks
// Add the sample references to the quicktime media.  
// The argument lastTime_ns will be timestamp of next sample that is not in store->audioSamples, or it will be set to duration of entire movie.
// Assume that sample references for all audio blocks have already been added to samples list store->audioSamples.
//
OSErr FinishAddingAudioBlocks(WebMImportGlobals store, long long lastTime_ns)
{
  OSErr err = noErr;
  const long long ns_per_sec = 1000000000;  // conversion factor, ns to sec

  //
  // Calculate duration value for each sample
  //

  long numSamples = store->audioSamples.size();
  long numTimes = store->audioTimes.size();
  if (numSamples != numTimes) {  
    dbg_printf("WebM Import - FinishAddingAudioBlocks - ERROR - parallel vectors with different sizes. numSamples=%d, numTimes=%d\n", numSamples, numTimes);
    // try to recover ****
  }
  dbg_printf("WebM Import - FinishAddingAudioBlocks - numSamples = %d, numTimes = %d\n", numSamples, numTimes);

  //SampleRefVec::iterator iter;
  //SampleTimeVec::iterator timeIter;
  //for (iter = store->samples.begin(); iter != store->samples.end(); ++iter) {
  long long blockDuration_ns = 0;
  for (long i = 0; i < numSamples; ++i) {
    if (i+1 < numSamples)
      blockDuration_ns = (store->audioTimes[i+1] - store->audioTimes[i]); 
    else
      blockDuration_ns = (lastTime_ns - store->audioTimes[i]);
    SoundDescriptionHandle sdh = store->audioDescHand;
    //dbg_printf("store->audioDescHand = %x\n", sdh);
    //dbg_printf("(*store->audioDescHand) = %xd\n", (*sdh));
    double sampleRate = (*sdh)->sampleRate; // **** store->audioDescHand is bogus here ****
    sampleRate = 48000.0; // **** hardcode sampling rate here to test.  
    TimeValue blockDuration_qt = static_cast<TimeValue>(double(blockDuration_ns) / ns_per_sec * sampleRate);  // GetMovieTimeScale(store->movie));  ****
    store->audioSamples[i].durationPerSample = blockDuration_qt;  // TimeValue, count of units

    dbg_printf("WebM Import - FinishAddingAudioBlocks - i = %d, Time = %lld, Duration = %ld\n", i, store->audioTimes[i], store->audioSamples[i].durationPerSample);
    dbg_printf("WebM Import - FinishAddingAudioBlocks - (blockDuration_ns = %lld, sampleRate = %7.3f\n", blockDuration_ns, sampleRate);
    // Alternatively, we can call AddMediaSampleReference() here inside loop, for each block. Supposedly more efficient to call it once after loop, specifying all blocks.
  }

  //
  // Add array of samples all at once (better performance than adding each one separately). 
  //
  SampleReferencePtr sampleRefs = &store->audioSamples.front(); // &store->audioSamples[0];
  err = AddMediaSampleReferences(store->movieAudioMedia, (SampleDescriptionHandle)store->audioDescHand, numSamples, sampleRefs, NULL);  // SampleReferencePtr
  if (err) {
    dbg_printf("WebM Import - FinishAddingAudioBlocks - AddMediaSampleRefereces() FAILED with err = %d\n", err);
  }
  
  // delete sample refs and times after they have been added to quicktime?

  return err;  
}


//--------------------------------------------------------------------------------
void DumpWebMDebugInfo(mkvparser::EBMLHeader* ebmlHeader, const mkvparser::SegmentInfo* webmSegmentInfo, const mkvparser::Tracks* webmTracks)
{
  // Header
  dbg_printf("EBMLHeader\n");
  dbg_printf("\tVersion\t\t: %lld\n", ebmlHeader->m_version);
  dbg_printf("\tMaxIDLength\t: %lld\n", ebmlHeader->m_maxIdLength);
  dbg_printf("\tMaxSizeLength\t: %lld\n", ebmlHeader->m_maxSizeLength);
  dbg_printf("\tDocType\t: %lld\n", ebmlHeader->m_docType);

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
      dbg_printf("\tCodec Id\t\t: %ls\n", codecID);
    if (codecName != NULL)
      dbg_printf("\tCodec Name\t\t: '%s'\n", codecName);
    
    if (trackType == VIDEO_TRACK) {
      const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const mkvparser::VideoTrack* const>(webmTrack);
      long long width = webmVideoTrack->GetWidth();
      long long height = webmVideoTrack->GetHeight();
      double rate = webmVideoTrack->GetFrameRate();
      dbg_printf("\t\twidth = %ld\n", width);
      dbg_printf("\t\theight = %ld\n", height);
      dbg_printf("\t\tframerate = %7.3f\n", rate);
    }
    
    if (trackType == AUDIO_TRACK) {
      // TODO: Audio track ****
      const mkvparser::AudioTrack* const webmAudioTrack = static_cast<const mkvparser::AudioTrack* const>(webmTrack);
      dbg_printf("\t\tAudio Track Sampling Rate: %7.3f\n", webmAudioTrack->GetSamplingRate());
      dbg_printf("\t\tAudio Track Channels: %d\n", webmAudioTrack->GetChannels());
      dbg_printf("\t\tAudio Track BitDepth: %lld\n", webmAudioTrack->GetBitDepth());
      
    }    
  }  // end track loop
  
}
