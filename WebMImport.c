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
  
  dbg_printf("[WebM Import]  >> [%08lx] :: FromDataRef(%d, %ld)\n", (UInt32) store, targetTrack != NULL, atTime);
  
  // The movieImportMustUseTrack flag indicates that we must use an existing track.
	// We don't support this and always create a new track, so return paramErr.
	if (inFlags & movieImportMustUseTrack)
		return paramErr;

  // try to use c++ lib
  long long pos = 0;
  MkvReaderQT reader;
  reader.Open(dataRef, dataRefType);
  using namespace mkvparser;
  EBMLHeader ebmlHeader;
  ebmlHeader.Parse(&reader, pos);
  
  dbg_printf("EBMLHeader\n");
  dbg_printf("EBMLHeader Version\t\t: %lld\n", ebmlHeader.m_version);
  dbg_printf("EBMLHeader MaxIDLength\t: %lld\n", ebmlHeader.m_maxIdLength);
  dbg_printf("EBMLHeader MaxSizeLength\t: %lld\n", ebmlHeader.m_maxSizeLength);
  dbg_printf("EBMLHeader DocType\t: %lld\n", ebmlHeader.m_docType);
  dbg_printf("Pos\t\t\t: %lld\n", pos);
  
#if 0
  // try to use c wrapper for libmkvreader and libmkvparser
  MkvReaderQT* reader = NULL;
  reader = CallMkvReaderQTInit();
  if (reader == NULL) goto bail;
  stat = CallMkvReaderQTOpen(reader, dataRef, dataRefType);
  if (stat) goto bail;
  
  
  // Get the File Header - synchronous read.
  // MkvReaderQT makes the DataHandler calls like DataHScheduleData()
  EBMLHeader header; // from hwasoo's lib?
  header = CallEBMLHeaderInit();
  CallEBMLHeaderParse(header, reader, long long* pos);
  fileOffset += pos;  //sizeof(header);
  
  // debug
  dbg_printf("EBML Header\n");
  dbg_printf("EBMLHeader version\t\t:  %lld\n", GetEBMLHeaderVersion(header));
  dbg_printf("EBMLHeader MaxIDLength\t: %lld\n", GetEBMLHeaderMaxIdLength(header));            
#endif
  
  
  // while {
  //    DataHScheduleData()
  //    NewMovieTrack()
  //    NewTrackMedia()
  //    SetTrackEnabled()
  //    AddMediaSampleReference()
  //    incr to next frame
  // InsertMediaIntoTrack()
  
  
  ImageDescriptionHandle videoDesc = NULL;
  Ptr colors = NULL;
  
  
  // ****
  
bail:
	if (videoTrack) {
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
