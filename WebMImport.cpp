// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include "WebMImport.hpp"

#include <Carbon/Carbon.h> 
#include <QuickTime/QuickTime.h>

#include "mkvparser.hpp"
#include "mkvreaderqt.hpp"

extern "C" {
#include "log.h"
}
  
typedef struct {
  ComponentInstance self;
  long  dataHOffset;
  long  fileSize;
  ComponentInstance dataHandler;
} WebMImportGlobalsRec, *WebMImportGlobals;

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
pascal ComponentResult WebMImportDataRef(WebMImportGlobals store, Handle dataRef, OSType dataRefType,
                                         Movie theMovie, Track targetTrack, Track *usedTrack,
                                         TimeValue atTime, TimeValue *durationAdded, 
                                         long inFlags, long *outFlags)
{
  OSErr err = noErr;
  ::Track movieVideoTrack = NULL;
  Media movieVideoMedia;
  ImageDescriptionHandle vp8DescHand = NULL;
  ComponentInstance dataHandler = 0;
  const long long ns_per_sec = 1000000000;  // conversion factor, ns to sec
  
  dbg_printf("[WebM Import]  >> [%08lx] :: FromDataRef(dataRef = %08lx, atTime = %ld)\n", (UInt32) store, (UInt32) dataRef, atTime); // targetTrack != NULL
  
  // The movieImportMustUseTrack flag indicates that we must use an existing track.
	// We don't support this and always create a new track, so return paramErr.
	if (inFlags & movieImportMustUseTrack)
		return paramErr;

  // Use IMkvReader subclass that knows about quicktime dataRef and dataHandler objects,
  // rather than plain file io.
  long long pos = 0;
  MkvReaderQT reader;
  int status = reader.Open(dataRef, dataRefType);
  if (status == 0)
    dbg_printf("MkvReaderQT::Open() returned success.\n");
  else
    dbg_printf("MkvReaderQT::Open() returned FAIL = %d\n", status);

  long long totalLength = 0;
  long long availLength = 0;
  reader.Length(&totalLength, &availLength);
  dbg_printf("reader.m_length = %lld bytes.\n", totalLength);
  
  // Use the libwebm project (libmkvparser.a) to parse the WebM file.

  //
  // WebM Header
  //
  using namespace mkvparser;
  EBMLHeader ebmlHeader;
  long long headerstatus = ebmlHeader.Parse(&reader, pos);
  dbg_printf("EBMLHeader.Parse() returned %lld.\n", headerstatus );
             
  dbg_printf("EBMLHeader\n");
  dbg_printf("EBMLHeader Version\t\t: %lld\n", ebmlHeader.m_version);
  dbg_printf("EBMLHeader MaxIDLength\t: %lld\n", ebmlHeader.m_maxIdLength);
  dbg_printf("EBMLHeader MaxSizeLength\t: %lld\n", ebmlHeader.m_maxSizeLength);
  dbg_printf("EBMLHeader DocType\t: %lld\n", ebmlHeader.m_docType);
  dbg_printf("Pos\t\t\t: %lld\n", pos);
  
#if 0
  fileOffset += pos;  //sizeof(header);  
#endif
  
  // Output debug info...
  //  DumpWebMFileInfo(); // cutnpaste from sample
  
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
  const long long timeCodeScale = webmSegmentInfo->GetTimeCodeScale();
  const long long duration = webmSegmentInfo->GetDuration();
  const wchar_t* const title = utf8towcs(webmSegmentInfo->GetTitleAsUTF8());
  // muxingApp, writingApp
  
  dbg_printf("Segment Info\n");
  dbg_printf("\t\tTimeCodeScale\t: %lld\n", timeCodeScale);
  dbg_printf("\t\tDuration\t\t: %lld ns\n", duration);  
  const double duration_sec = double(duration) / 1000000000;
  dbg_printf("\t\tDuration\t\t: %7.3f sec\n", duration_sec);
  
  dbg_printf("\t\tPosition(Segment)\t: %lld\n", webmSegment->m_start); // position of segment payload
  dbg_printf("\t\tSize(Segment)\t\t: %lld\n", webmSegment->m_size);  // size of segment payload

  //
  // WebM Tracks
  //
  mkvparser::Tracks* const webmTracks = webmSegment->GetTracks();
  unsigned long trackIndex = 0;
  const unsigned long numTracks = webmTracks->GetTracksCount();
  enum { VIDEO_TRACK = 1, AUDIO_TRACK = 2 };

  while (trackIndex != numTracks) {
    const mkvparser::Track* const webmTrack = webmTracks->GetTrackByIndex(trackIndex++);
    if (webmTrack == NULL)
      continue;
    
    // get track info
    unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
    unsigned long trackNum = webmTrack->GetNumber();
    const wchar_t* const trackName = utf8towcs(webmTrack->GetNameAsUTF8());
    const char* const codecID = webmTrack->GetCodecId();
    const wchar_t* const codecName = utf8towcs(webmTrack->GetCodecNameAsUTF8());

    // debug print
    dbg_printf("Track Type\t\t: %ld\n", trackType);
    dbg_printf("Track Number\t\t: %ld\n", trackNum);    
    if (codecID != NULL)
      dbg_printf("Codec Id\t\t: %ls\n", codecID);
    if (codecName != NULL)
      dbg_printf("Code Name\t\t: '%s'\n", codecName);
    
    if (trackType == VIDEO_TRACK) {
      const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const VideoTrack* const>(webmTrack);
      long long width = webmVideoTrack->GetWidth();
      long long height = webmVideoTrack->GetHeight();
      double rate = webmVideoTrack->GetFrameRate();
      dbg_printf("\t\twidth = %ld\n", width);
      dbg_printf("\t\theight = %ld\n", height);
      dbg_printf("\t\tframerate = %7.3f\n", rate);
    }

    if (trackType == AUDIO_TRACK) {
      // TODO: Audio track ****
    }    
  }  // end track loop
  
  const unsigned long clusterCount = webmSegment->GetCount();
  if (clusterCount == 0) {
    dbg_printf("Segment has no Clusters!\n");
    delete webmSegment;
    return -1;
  }
  dbg_printf("Cluster Count\t\t: %ld\n", clusterCount);
  
  // Remember previous block, add them one behind
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
    const BlockEntry* webmBlockEntry = webmCluster->GetFirst();
    dbg_printf("TIME - Cluster Time: %lld\n", time_ns); // ****
    
    //
    //  WebM Block
    //
    while ((webmBlockEntry != NULL) && (!webmBlockEntry->EOS()))
    {
      const mkvparser::Block* const webmBlock = webmBlockEntry->GetBlock();
      const unsigned long trackNum = webmBlock->GetTrackNumber(); // block's track number (see 
      const mkvparser::Track* webmTrack = webmTracks->GetTrackByNumber(trackNum);
      const unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
      const long blockSize = webmBlock->m_size; // was webmBlock->GetSize();
      const long long blockTime_ns = webmBlock->GetTime(webmCluster);
      // dbg_printf("libwebm block->GetSize=%ld, block->m_size=%lld\n", webmBlock->GetSize(), webmBlock->m_size);
      dbg_printf("libwebm block->GetOffset=%ld, block->m_start=%lld\n", webmBlock->GetOffset(), webmBlock->m_start);
                 
      dbg_printf("TIME - Block Time: %lld\n", blockTime_ns);
      dbg_printf("\t\t\tBlock\t\t:%s,%15ld,%s,%15lld\n", (trackType == VIDEO_TRACK) ? "V" : "A", blockSize, webmBlock->IsKey() ? "I" : "P", blockTime_ns);
      
      if (trackType == VIDEO_TRACK) {
        //
        // WebM Video Data
        //
        const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const VideoTrack* const>(webmTrack);
        long long width = webmVideoTrack->GetWidth();
        long long height = webmVideoTrack->GetHeight();
        
#if 0
        // DEBUG: read frame data from WebM Block into buffer
        unsigned char* buf = (unsigned char*)malloc(blockSize);  // vp8 frame
        status = webmBlock->Read(&reader, buf);
        long long *firstEightBytes;
        firstEightBytes = (long long*)buf;
        dbg_printf("BlockData = %llx\n", firstEightBytes);
#endif        
        //
        // QuickTime movie stuff begins here...
        //
        // while {
        //    NewMovieTrack()
        //    NewTrackMedia()
        //    SetTrackEnabled()
        //    AddMediaSampleReference()
        //    incr to next frame
        // InsertMediaIntoTrack()
        // GetTrackDuration()

#pragma mark QTStuff

        // Create image description so QT can find appropriate decoder component (our VP8 decoder)
        err = CreateVP8ImageDescription(width, height, &vp8DescHand);
        if (err) goto bail;
        if (movieVideoTrack == NULL) {  // the quicktime movie track (not WebM file track)

          // Create a new QT video track
          movieVideoTrack = NewMovieTrack(theMovie, (**vp8DescHand).width << 16, (**vp8DescHand).height << 16, kNoVolume);
          
          // Create a new QT media for the track
          // (The media refers to the actual data samples used by the track.)
          // TimeScale timeScale  = timeCodeScale; // try using value retreived from WebM SegmentInfo above. NO
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
        
        // fileOffset += frameSize;   // No, we can get next fileOffset directly from next webmBlock.  Dont need to calculate.
      } // end video track
            
      // Advance to next webm Block within this Cluster.
      webmBlockEntry = webmCluster->GetNext(webmBlockEntry);
    } // end block loop
    
    // Advance to next webm Cluster
    webmCluster = webmSegment->GetNext(webmCluster);
  } // end cluster loop
 
  // Add the last block in the file now, using the "prevBlock" fields stored above. 
  if (prevBlock != 0) {
    // Calculate duration of last block by subtracting time from duration of entire segment
    long long blockDuration_ns = (duration - prevBlockTime);    // ns, or (nextFrameTime - frameTime)
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
  
  // Insert the added media into the track
  // Time value specifying where the segment is to be inserted in the movie's time scale, -1 to add the media data to the end of the track
  TimeValue mediaDuration = GetMediaDuration(movieVideoMedia);
  err = InsertMediaIntoTrack(movieVideoTrack, atTime, 0, mediaDuration, fixed1);
  if (err) goto bail;
  
  // Return the duration added 
  if (movieVideoTrack != NULL)
    *durationAdded = GetTrackDuration(movieVideoTrack) - atTime;
  
bail:
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
	if (usedTrack) *usedTrack = movieVideoTrack;

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
// CreateImageDescriptionVP8
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
