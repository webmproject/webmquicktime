/*
 *  WebMImport.c
 *  WebM
 *
 *  Created by Jeffrey Koppi on 8/5/10.
 *  Copyright 2010 Google Inc. All rights reserved.
 *
 */


#include "WebMImport.h"

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>

#include "log.h"

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


// Component Close Request - Required
pascal ComponentResult WebMImportClose(WebMImportGlobals store, ComponentInstance self)
{
  dbg_printf("[WebM Import]  >> [%08lx] :: Close()\n", (UInt32) store);

  if (store)
    DisposePtr((Ptr)store);

  dbg_printf("[WebM Import] <   [%08lx] :: Close()\n", (UInt32) store);
  return noErr;
}

// Component Version Request - Required
pascal ComponentResult WebMImportVersion(WebMImportGlobals store)
{
  dbg_printf("[WebM Import]  >> [%08lx] :: Version()\n", (UInt32) store);
  dbg_printf("[WebM Import] <   [%08lx] :: Version()\n", (UInt32) store);
  return kWebMImportVersion;
}

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


// MovieImportDataRef
pascal ComponentResult WebmImportDataRef(WebMImportGlobals store, Handle dataRef, OSType dataRefType,
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

  // Retrieve the best data handler component to use with the given data reference, for read purpoases.
	// Then open the returned component using standard Component Manager calls.
	err = OpenAComponent(GetDataHandler(dataRef, dataRefType, kDataHCanRead), &dataHandler);
	if (err) goto bail;
 
  // Provide a data reference to the data handler.
	// Once you have assigned a data reference to the data handler, you may start reading and/or writing
	// movie data from that data reference.
	err = DataHSetDataRef(dataHandler, dataRef);
	if (err) goto bail;
  
  // Open a read path to the current data reference. 
  // You need to do this before your component can read data using a data handler component.
	err = DataHOpenForRead(dataHandler);
	if (err) goto bail;
  
  // Get the size, in bytes, of the current data reference.
  // This is functionally equivalent to the File Manager's GetEOF function.
  long fileSize = 0;
	err = DataHGetFileSize(dataHandler, &fileSize);
	if (err) goto bail;

  // Get the File Header - synchronous read
	// This function provides both a synchronous and an asynchronous read interface...
  //ImageHeader header;//***** 
  EBMLHeader header; // from hwasoo's lib?
  // (DataHandler component instance, memory to receive data, offset into data ref to read from, number of bytes to read, refcon, schedul rec NULL for sync, completion func NULL for sync)
  err = DataHScheduleData(dataHandler, (Ptr)&header, fileOffset, sizeof(header), 0, NULL, NULL);  
  if (err) goto bail;
  
  fileOffset += sizeof(header);
  
  
  
  // while
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
  
	return err;  
}


// MovieImportValidate
// MovieImportGetMimeTypeList
// MovieImportValidateDataRef
// MovieImportRegister

