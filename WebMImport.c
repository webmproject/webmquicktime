// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include "WebMImport.h"

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>

//#include "mkvreader_wrap.h"
//#include "mkvparser_wrap.h"
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
  Track videoTrack = NULL;
  ComponentInstance dataHandler = 0;
  
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
  // *** muxingApp, writingApp
  
  dbg_printf("Segment Info\n");
  dbg_printf("TimeCodeScale\t: %lld\n", timeCodeScale);
  dbg_printf("Duration\t\t: %lld ns\n", duration);  
  const double duration_sec = double(duration) / 1000000000;
  dbg_printf("Duration\t\t: %7.3f sec\n", duration_sec);
  
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
    if (trackType == VIDEO_TRACK) {
      const mkvparser::VideoTrack* const webmVideoTrack = static_cast<const VideoTrack* const>(webmTrack);
      long long width = webmVideoTrack->GetWidth();
      long long height = webmVideoTrack->GetHeight();
      double rate = webmVideoTrack->GetFrameRate();
    }

    if (trackType == AUDIO_TRACK) {
      // **** Audio track
    }
    
    // debug print
    dbg_printf("Track Type\t\t: %ld\n", trackType);
    dbg_printf("Track Number\t: %ld\n", trackNum);    
    if (codecID != NULL)
      dbg_printf("Codec Id\t\t: %ls\n", codecID);
    if (codecName != NULL)
      dbg_printf("Code Name\t\t: %s\n", codecName);
    
  }  // end track loop
  
  const unsigned long clusterCount = webmSegment->GetCount();
  if (clusterCount == 0) {
    dbg_printf("Segment has no Clusters!\n");
    delete webmSegment;
    return -1;
  }
  
  //
  //  WebM Cluster
  //
  mkvparser::Cluster* webmCluster = webmSegment->GetFirst();
  while ((webmCluster != NULL) && !webmCluster->EOS()) 
  {
    const long long timeCode = webmCluster->GetTimeCode();
    const long long time_ns = webmCluster->GetTime();
    const BlockEntry* webmBlockEntry = webmCluster->GetFirst();
    //
    //  WebM Block
    //
    while ((webmBlockEntry != NULL) && (!webmBlockEntry->EOS()))
    {
      const mkvparser::Block* const webmBlock = webmBlockEntry->GetBlock();
      const unsigned long trackNum = webmBlock->GetTrackNumber(); // block's track number (see 
      const mkvparser::Track* webmTrack = webmTracks->GetTrackByNumber(trackNum);
      const unsigned long trackType = static_cast<unsigned long>(webmTrack->GetType());
      const long size = webmBlock->GetSize();
      const long long time_ns = webmBlock->GetTime(webmCluster);

      dbg_printf("\t\t\tBlock\t\t:%s,%15ld,%s,%15lld\n",
             (trackType == VIDEO_TRACK) ? "V" : "A",
             size,
             webmBlock->IsKey() ? "I" : "P",
             time_ns);
      
      if (trackType == VIDEO_TRACK) {
        //
        // Data
        //

        // read frame data from WebM Block into buffer
        unsigned char* buf = (unsigned char*)malloc(size);
        status = webmBlock->Read(&reader, buf);

        // ****
        
      }
      
      webmBlockEntry = webmCluster->GetNext(webmBlockEntry);
    }
    
    webmCluster = webmSegment->GetNext(webmCluster);
  }
  
  
  // while {
  //    NewMovieTrack()
  //    NewTrackMedia()
  //    SetTrackEnabled()
  //    AddMediaSampleReference()
  //    incr to next frame
  // InsertMediaIntoTrack()
  // GetTrackDuration()
  
  
  ImageDescriptionHandle videoDesc = NULL;
  Ptr colors = NULL;
  
  
  // ****
  
bail:
	if (videoTrack) { // QT videoTrack
		if (err) {
			DisposeMovieTrack(videoTrack);
			videoTrack = NULL;
		} else {
			// Set the outFlags to reflect what was done.
			*outFlags |= movieImportCreateTrack;
		}
	}
	if (videoDesc)
		DisposeHandle((Handle)videoDesc);
  
	if (colors)
		DisposePtr(colors);
  
	// Remember to close what you open.
	if (dataHandler)
		CloseComponent(dataHandler);
  
	// Return the track identifier of the track that received the imported data in the usedTrack pointer. Your component
	// needs to set this parameter only if you operate on a single track or if you create a new track. If you modify more
	// than one track, leave the field referred to by this parameter unchanged. 
	if (usedTrack) *usedTrack = videoTrack;

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
