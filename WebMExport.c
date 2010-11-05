// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#if defined(__APPLE_CC__)
#include <QuickTime/QuickTime.h>
#include <Carbon/Carbon.h>
#else
#include <QuickTimeComponents.h>
#endif

#include "log.h"
#include "WebMExportStructs.h"
#include "WebMExportVersions.h"
#include "VP8CodecVersion.h"
#include "WebMExport.h"



#include "WebMExportGui.h"
/* component selector methods, TODO find out why only these 3 need to be declared */
pascal ComponentResult WebMExportGetComponentPropertyInfo(WebMExportGlobalsPtr   store,
                                                          ComponentPropertyClass inPropClass,
                                                          ComponentPropertyID    inPropID,
                                                          ComponentValueType     *outPropType,
                                                          ByteCount              *outPropValueSize,
                                                          UInt32                 *outPropertyFlags);
pascal ComponentResult WebMExportGetComponentProperty(WebMExportGlobalsPtr  store,
                                                      ComponentPropertyClass inPropClass,
                                                      ComponentPropertyID    inPropID,
                                                      ByteCount              inPropValueSize,
                                                      ComponentValuePtr      outPropValueAddress,
                                                      ByteCount              *outPropValueSizeUsed);
pascal ComponentResult WebMExportSetComponentProperty(WebMExportGlobalsPtr  store,
                                                      ComponentPropertyClass inPropClass,
                                                      ComponentPropertyID    inPropID,
                                                      ByteCount              inPropValueSize,
                                                      ConstComponentValuePtr inPropValueAddress);



static void CloseAllStreams(WebMExportGlobalsPtr store);

static OSErr ConfigureQuickTimeMovieExporter(WebMExportGlobalsPtr store);

static ComponentResult _getFrameRate(Movie theMovie, double *fps);

static ComponentResult getMovieDimensions(Movie theMovie, Fixed *width, Fixed *height);


#define CALLCOMPONENT_BASENAME()        WebMExport
#define CALLCOMPONENT_GLOBALS()         WebMExportGlobalsPtr storage

#define MOVIEEXPORT_BASENAME()          CALLCOMPONENT_BASENAME()
#define MOVIEEXPORT_GLOBALS()           CALLCOMPONENT_GLOBALS()

#define COMPONENT_UPP_SELECT_ROOT() MovieExport
#define COMPONENT_DISPATCH_FILE     "WebMExportDispatch.h"

#if !TARGET_OS_WIN32
#include <CoreServices/Components.k.h>
#include <QuickTime/QuickTimeComponents.k.h>
#include <QuickTime/ImageCompression.k.h>   // for ComponentProperty selectors
#include <QuickTime/ComponentDispatchHelper.c>
#else
#include <Components.k.h>
#include <QuickTimeComponents.k.h>
#include <ImageCompression.k.h>
#include <ComponentDispatchHelper.c>
#endif


pascal ComponentResult WebMExportOpen(WebMExportGlobalsPtr store, ComponentInstance self)
{
  ComponentDescription cd;
  ComponentResult err;
  
  dbg_printf("[WebM -- %08lx] Open()\n", (UInt32) store);
  
  store = (WebMExportGlobalsPtr) NewPtrClear(sizeof(WebMExportGlobals));
  err = MemError();
  
  if (!err)
  {
    store->self = self;
    store->framerate = 0;
    
    store->bExportVideo = 1;
    store->bExportAudio = 1;
    
    store->bAltRefEnabled = 0;
    
    store->audioSettingsAtom = NULL;
    store->videoSettingsAtom = NULL;
    store->videoSettingsCustom = NULL;
    store->streams = NULL;
    store->streamCount = 0;
    store->cueHandle = NULL;
    store->cueCount = 0;
    
    memset(&store->audioBSD, 0, sizeof(AudioStreamBasicDescription));
    
    store->audioBSD.mFormatID = kAudioFormatXiphVorbis;
    store->audioBSD.mChannelsPerFrame = 1;
    store->audioBSD.mSampleRate = 44100.000;
    
    store->bMovieHasAudio = true;
    store->bMovieHasVideo = true;
    
    store->webmTimeCodeScale = 1000000; ///TODO figure out about how to use this
    
    SetComponentInstanceStorage(self, (Handle) store);
    
    cd.componentType = MovieExportType;
    cd.componentSubType = kQTFileTypeMovie;
    cd.componentManufacturer = kAppleManufacturer;
    cd.componentFlags = canMovieExportFromProcedures | movieExportMustGetSourceMediaType;
    cd.componentFlagsMask = cd.componentFlags;
    
    err = OpenAComponent(FindNextComponent(NULL, &cd), &store->quickTimeMovieExporter);
  }
  
  dbg_printf("[WebM %08lx] Exit Open()\n", (UInt32) store);
  return err;
}


pascal ComponentResult WebMExportClose(WebMExportGlobalsPtr store, ComponentInstance self)
{
  dbg_printf("[WebM -- %08lx] :: Close()\n", (UInt32) store);
  
  if (store)
  {
    if (store->quickTimeMovieExporter)
      CloseComponent(store->quickTimeMovieExporter);
    
    CloseAllStreams(store);
    
    if (store->videoSettingsAtom)
      QTDisposeAtomContainer(store->videoSettingsAtom);
    
    if (store->audioSettingsAtom)
      QTDisposeAtomContainer(store->audioSettingsAtom);
    if(store->videoSettingsCustom)
      DisposeHandle(store->videoSettingsCustom);
    
    DisposePtr((Ptr) store);
  }
  
  dbg_printf("[WebM -- %08lx] :: Close()\n", (UInt32) store);
  return noErr;
}

pascal ComponentResult WebMExportVersion(WebMExportGlobalsPtr store)
{
  return kWebM_spit__Version;
}

// seems that currently kComponentDataTypeCFDataRef is private so I am using 'cfdt'
static const ComponentPropertyInfo kExportProperties[] = 
{
  { kComponentPropertyClassPropertyInfo,   kComponentPropertyInfoList, 'cfdt', sizeof(CFDataRef), kComponentPropertyFlagCanGetNow | kComponentPropertyFlagValueIsCFTypeRef | kComponentPropertyFlagValueMustBeReleased }
};

// GetComponentPropertyInfo
// Component Property Info request - Optional but good practice for QuickTime 7 forward
// Returns information about the properties of a component
pascal ComponentResult WebMExportGetComponentPropertyInfo(WebMExportGlobalsPtr   store,
                                                          ComponentPropertyClass inPropClass,
                                                          ComponentPropertyID    inPropID,
                                                          ComponentValueType     *outPropType,
                                                          ByteCount              *outPropValueSize,
                                                          UInt32                 *outPropertyFlags)
{
#pragma unused (store)
  
  ComponentResult err = kQTPropertyNotSupportedErr;
  
  switch (inPropClass) {
    case kComponentPropertyClassPropertyInfo:
      switch (inPropID) {
        case kComponentPropertyInfoList:
          if (outPropType) *outPropType = kExportProperties[0].propType;
          if (outPropValueSize) *outPropValueSize = kExportProperties[0].propSize;
          if (outPropertyFlags) *outPropertyFlags = kExportProperties[0].propFlags;
          err = noErr;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
  if (err != noErr)
    dbg_printf("[webm] error in WebMExportGetComponentPropertyInfo %d \n", err);
  return err;
  
}

// GetComponentProperty
// Get Component Property request - Optional but good practice for QuickTime 7 forward
// Returns the value of a specific component property
pascal ComponentResult WebMExportGetComponentProperty(WebMExportGlobalsPtr  store,
                                                      ComponentPropertyClass inPropClass,
                                                      ComponentPropertyID    inPropID,
                                                      ByteCount              inPropValueSize,
                                                      ComponentValuePtr      outPropValueAddress,
                                                      ByteCount              *outPropValueSizeUsed)
{
	ByteCount size = 0;
	UInt32 flags = 0;
  CFDataRef *outPropCFDataRef;
  
  ComponentResult err = noErr;
  
  // sanity check
  if (NULL == outPropValueAddress) return paramErr;
  
  err = QTGetComponentPropertyInfo(store->self, inPropClass, inPropID, NULL, &size, &flags);
  if (err) goto bail;
  
  if (size > inPropValueSize) return kQTPropertyBadValueSizeErr;
  
  if (flags & kComponentPropertyFlagCanGetNow) {
    switch (inPropID) {
      case kComponentPropertyInfoList:
        outPropCFDataRef = (CFDataRef *)outPropValueAddress;
        *outPropCFDataRef = CFDataCreate(kCFAllocatorDefault, (UInt8 *)((ComponentPropertyInfo *)kExportProperties), sizeof(kExportProperties));
        if (outPropValueSizeUsed) *outPropValueSizeUsed = size;
        break;
      default:
        break;
    }
  }
  
bail:
  return err;
}

// SetComponentProperty
// Set Component Property request - Optional but good practice for QuickTime 7 forward
// Sets the value of a specific component property
pascal ComponentResult WebMExportSetComponentProperty(WebMExportGlobalsPtr  store,
                                                      ComponentPropertyClass inPropClass,
                                                      ComponentPropertyID    inPropID,
                                                      ByteCount              inPropValueSize,
                                                      ConstComponentValuePtr inPropValueAddress)
{
  ComponentResult err = noErr;
  dbg_printf("[WebM %08lx] :: SetComponentProperty('%4.4s', '%4.4s', %ld)\n", (UInt32) store, (char *) &inPropClass, (char *) &inPropID, inPropValueSize);
  //just pass on the property
  err = QTSetComponentProperty(store->quickTimeMovieExporter, inPropClass,
                               inPropID, inPropValueSize, inPropValueAddress);
  
  
  dbg_printf("[WebM] <   [%08lx] :: SetComponentProperty() = %ld\n", (UInt32) store, err);
  return err;
}

//  ExportValidate 
//		Determines whether a movie export component can export all the data for a specified movie or track.
// This function allows an application to determine if a particular movie or track could be exported by the specified
// movie data export component. The movie or track is passed in the theMovie and onlyThisTrack parameters as they are
// passed to MovieExportToFile. Although a movie export component can export one or more media types, it may not be able
// to export all the kinds of data stored in those media. Movie data export components that implement this function must
// also set the canMovieExportValidateMovie flag.
pascal ComponentResult WebMExportValidate(WebMExportGlobalsPtr store, Movie theMovie, Track onlyThisTrack, Boolean *valid)
{
  OSErr err;
  
  dbg_printf("[WebM -- %08lx] :: Validate()\n", (UInt32) store);
  
  err = MovieExportValidate(store->quickTimeMovieExporter, theMovie, onlyThisTrack, valid);
  
  if (!err)
  {
    if (*valid == true)
    {
      
      if (onlyThisTrack == NULL)
      {
        if (GetMovieIndTrackType(theMovie, 1, VisualMediaCharacteristic, movieTrackCharacteristic | movieTrackEnabledOnly) == NULL &&
            GetMovieIndTrackType(theMovie, 1, AudioMediaCharacteristic, movieTrackCharacteristic | movieTrackEnabledOnly) == NULL)
          *valid = false;
      }
      else
      {
        MediaHandler mh = GetMediaHandler(GetTrackMedia(onlyThisTrack));
        Boolean hasIt = false;
        
        MediaHasCharacteristic(mh, VisualMediaCharacteristic, &hasIt);
        
        if (hasIt == false)
          MediaHasCharacteristic(mh, AudioMediaCharacteristic, &hasIt);
        
        if (hasIt == false)
          *valid = false;
      }
    }
  }
  
  dbg_printf("[WebM] <   [%08lx] :: Validate() = %d, %d\n", (UInt32) store, err, *valid);
  return err;
}


// MovieExportToFile
// 		Exports data to a file. The requesting program or Movie Toolbox must create the destination file
// before calling this function. Your component may not destroy any data in the destination file. If you
// cannot add data to the specified file, return an appropriate error. If your component can write data to
// a file, be sure to set the canMovieExportFiles flag in the componentFlags field of your component's
// ComponentDescription structure. Your component must be prepared to perform this function at any time.
// You should not expect that any of your component's configuration functions will be called first. 
pascal ComponentResult WebMExportToFile(WebMExportGlobalsPtr store, const FSSpec *theFilePtr,
                                        Movie theMovie, Track onlyThisTrack, TimeValue startTime,
                                        TimeValue duration)
{
  AliasHandle alias;
  ComponentResult err;
  
  dbg_printf("[WebM -- %08lx] ToFile(%d, %ld, %ld)\n", (UInt32) store, onlyThisTrack != NULL, startTime, duration);
  
  err = QTNewAlias(theFilePtr, &alias, true);
  if (!err) goto bail;
  
  err = MovieExportToDataRef(store->self, (Handle) alias, rAliasType, theMovie, onlyThisTrack, startTime, duration);
  DisposeHandle((Handle) alias);
  
bail:
  dbg_printf("[WebM -- %08lx] ToFile()\n", (UInt32) store);
  return err;
}

// MovieExportToDataRef
//      Allows an application to request that data be exported to a data reference.
pascal ComponentResult WebMExportToDataRef(WebMExportGlobalsPtr store, Handle dataRef, OSType dataRefType,
                                           Movie theMovie, Track onlyThisTrack, TimeValue startTime, TimeValue duration)
{
  TimeScale scale;
  MovieExportGetPropertyUPP getVideoPropertyProc = NULL;
  MovieExportGetDataUPP getVideoDataProc = NULL;
  MovieExportGetPropertyUPP getSoundPropertyProc = NULL;
  MovieExportGetDataUPP getSoundDataProc = NULL;
  void *videoRefCon;
  void *audioRefCon;
  long trackID;
  ComponentResult err;
  Boolean have_sources = false;
  
  dbg_printf("[WebM -- %08lx] ToDataRef(%d, %ld, %ld)\n", (UInt32) store, onlyThisTrack != NULL, startTime, duration);
  dbg_printf("[WebM] ToDataRef -- bMovieHasAudio %d, bMovieHasVideo %d, bExportAudio %d, bExportVideo %d\n",
             store->bMovieHasAudio, store->bMovieHasVideo, store->bExportAudio, store->bExportVideo);
  
  if (store->bExportVideo && store->bMovieHasVideo)
  {
    err = MovieExportNewGetDataAndPropertiesProcs(store->quickTimeMovieExporter, VideoMediaType, &scale, theMovie,
                                                  onlyThisTrack, startTime, duration, &getVideoPropertyProc,
                                                  &getVideoDataProc, &videoRefCon);
    dbg_printf("[WebM]   # [%08lx] :: ToDataRef() = %ld\n", (UInt32) store, err);
    
    if (err) goto bail;
    
    err = MovieExportAddDataSource(store->self, VideoMediaType, scale, &trackID, getVideoPropertyProc, getVideoDataProc, videoRefCon);
    if (err) goto bail;      
    have_sources = true;
    
    if (store->framerate == 0)
      _getFrameRate(theMovie, &store->framerate);
  }
  
  if (store->bExportAudio && store->bMovieHasAudio)
  {
    err = MovieExportNewGetDataAndPropertiesProcs(store->quickTimeMovieExporter, SoundMediaType, &scale, theMovie,
                                                  onlyThisTrack, startTime, duration, &getSoundPropertyProc,
                                                  &getSoundDataProc, &audioRefCon);
    
    dbg_printf("[WebM]   = [%08lx] :: ToDataRef() = %ld\n", (UInt32) store, err);
    
    if (err) goto bail;
    // ** Add the audio data source **
    err = MovieExportAddDataSource(store->self, SoundMediaType, scale, &trackID, getSoundPropertyProc, getSoundDataProc, audioRefCon);
    
    if (err) goto bail;
    
    have_sources = true;
  }
  
  if (have_sources)
    err = MovieExportFromProceduresToDataRef(store->self, dataRef, dataRefType);
  else
    err = invalidMovie;
  
  if (err) goto bail;
  
  if (getSoundPropertyProc || getSoundDataProc)
    MovieExportDisposeGetDataAndPropertiesProcs(store->quickTimeMovieExporter, getSoundPropertyProc, getSoundDataProc, audioRefCon);
  
  if (getVideoPropertyProc || getVideoDataProc)
    MovieExportDisposeGetDataAndPropertiesProcs(store->quickTimeMovieExporter, getVideoPropertyProc, getVideoDataProc, videoRefCon);
bail:
  dbg_printf("[WebM] <   [%08lx] :: ToDataRef() = %d, %ld\n", (UInt32) store, err, trackID);
  return err;
}

// MovieExportFromProceduresToDataRef
//		Exports data provided by MovieExportAddDataSource to a location specified by dataRef and dataRefType.
// Movie data export components that support export operations from procedures must set the canMovieExportFromProcedures
// flag in their component flags. 
pascal ComponentResult WebMExportFromProceduresToDataRef(WebMExportGlobalsPtr store, Handle dataRef, OSType dataRefType)
{
  DataHandler    dataH = NULL;
  ComponentResult err;
  
  dbg_printf("[WebM--%08lx] FromProceduresToDataRef()\n", (UInt32) store);
  
  if (store->streamCount == 0)
    return noErr;  //no data to write
  
  if (!dataRef || !dataRefType)
    return paramErr;
  
  // Get and open a Data Handler Component that can write to the dataRef
  err = OpenAComponent(GetDataHandler(dataRef, dataRefType, kDataHCanWrite), &dataH);
  if (err) goto bail;
  
  DataHSetDataRef(dataH, dataRef);
  
  err = DataHCreateFile(dataH, FOUR_CHAR_CODE('TVOD'), true);
  if (err) goto bail;
  
  DataHSetMacOSFileType(dataH, FOUR_CHAR_CODE('webm'));
  err = DataHOpenForWrite(dataH);
  if (err) goto bail;
  
  err = ConfigureQuickTimeMovieExporter(store);
  if (err) goto bail;
  
  err = muxStreams(store, dataH);
  
bail:
  
  if (dataH)
    CloseComponent(dataH);
  
  dbg_printf("[WebM--%08lx] FromProceduresToDataRef() = %ld\n", (UInt32) store, err);
  return err;
}



pascal ComponentResult WebMExportNewGetDataAndPropertiesProcs(WebMExportGlobalsPtr store, OSType trackType, TimeScale *scale, Movie theMovie,
                                                              Track theTrack, TimeValue startTime, TimeValue duration,
                                                              MovieExportGetPropertyUPP *propertyProc, MovieExportGetDataUPP *getDataProc,
                                                              void **refCon)
{
  ComponentResult err;
  dbg_printf("[WebM -- %08lx] NewGetDataAndPropertiesProcs(%4.4s, %ld, %ld)\n", (UInt32) store, (char *) &trackType, startTime, duration);
  
  err = MovieExportNewGetDataAndPropertiesProcs(store->quickTimeMovieExporter, trackType, scale, theMovie, theTrack, startTime, duration,
                                                propertyProc, getDataProc, refCon);
  return err;
}

pascal ComponentResult WebMExportDisposeGetDataAndPropertiesProcs(WebMExportGlobalsPtr store,
                                                                  MovieExportGetPropertyUPP propertyProc, MovieExportGetDataUPP getDataProc,
                                                                  void *refCon)
{
  ComponentResult err;
  err = MovieExportDisposeGetDataAndPropertiesProcs(store->quickTimeMovieExporter, propertyProc, getDataProc, refCon);
  dbg_printf("[WebM--%08lx]  DisposeGetDataAndPropertiesProcs(%08lx) , err %d\n", (UInt32) store, (UInt32) refCon, err);
  return err;
}

static void _addNewStream(WebMExportGlobalsPtr store)
{
  store->streamCount ++;
  
  if (store->streams)
    SetHandleSize((Handle) store->streams, sizeof(GenericStream) * store->streamCount);
  else
    store->streams = (GenericStream **) NewHandleClear(sizeof(GenericStream));
  
  dbg_printf("[webm] adding stream %d size is %ld\n", store->streamCount, sizeof(GenericStream) * store->streamCount);
}

pascal ComponentResult WebMExportAddDataSource(WebMExportGlobalsPtr store, OSType trackType, TimeScale scale,
                                               long *trackIDPtr, MovieExportGetPropertyUPP propertyProc,
                                               MovieExportGetDataUPP getDataProc, void *refCon)
{
  ComponentResult err = noErr;
  
  dbg_printf("[WebM - %08lx] AddDataSource('%4.4s')\n", (UInt32) store, (char *) &trackType);
  
  *trackIDPtr = 0;
  
  if (!scale || !trackType || !getDataProc || !propertyProc)
    return paramErr;
  
  if (trackType == VideoMediaType || trackType == SoundMediaType)
  {
    _addNewStream(store);
    *trackIDPtr = store->streamCount;
    
    GenericStream *gs = &(*store->streams)[store->streamCount-1];
    gs->trackType = trackType;
    StreamSource *source = NULL;
    
    if (trackType == VideoMediaType)
    {
      VideoStreamPtr p = &gs->vid;
      initVideoStream(p);
    }
    else if (trackType == SoundMediaType)
    {
      initAudioStream(gs);
    }
    initFrameQueue(&gs->frameQueue);
    
    initStreamSource(&gs->source, scale, *trackIDPtr,  propertyProc,
                     getDataProc, refCon);
  }
  else
  {
    dbg_printf("[WebM] ignoring stream %d\n", trackType);
  }
  
  return err;
}

pascal ComponentResult WebMExportSetProgressProc(WebMExportGlobalsPtr store, MovieProgressUPP proc, long refCon)
{
  dbg_printf("[WebM - %08lx] SetProgressProc()\n", (UInt32) store);
  
  store->progressProc = proc;
  store->progressRefCon = refCon;
  return noErr;
}



pascal ComponentResult WebMExportDoUserDialog(WebMExportGlobalsPtr store, Movie theMovie, Track onlyThisTrack,
                                              TimeValue startTime, TimeValue duration, Boolean *canceledPtr)
{
  WindowRef   window = NULL;
  Boolean     portChanged = false;
  
  CGrafPtr    savedPort;
  OSErr       err = resFNotFound;
  Boolean previousAudioExport = store->bExportAudio; 
  Boolean previousVideoExport = store->bExportVideo;
  
  EventTypeSpec eventList[] = {{kEventClassCommand, kEventCommandProcess}};
  EventHandlerUPP settingsWindowEventHandlerUPP = NewEventHandlerUPP(SettingsWindowEventHandler);
  
  dbg_printf("[WebM %08lx] DoUserDialog()\n", (UInt32) store);
  getWindow(&window);
  
  portChanged = QDSwapPort(GetWindowPort(window), &savedPort);
  
  *canceledPtr = false;
  
  err = checkMovieHasVideoAudio(store, theMovie, onlyThisTrack);
  
  
  dbg_printf("[WebM] DoUserDialog() End theMovie Block -- allow Aud %d, allow Vid %d, aud disable %d, vid disable %d\n",
             store->bMovieHasAudio, store->bMovieHasVideo, store->bExportAudio, store->bExportVideo);
  enableDisableControls(store, window);
  
  store->setdlg_movie = theMovie;
  store->setdlg_track = onlyThisTrack;
  
  InstallWindowEventHandler(window, settingsWindowEventHandlerUPP, GetEventTypeCount(eventList), eventList, store, NULL);
  
  ShowWindow(window);
  
  RunAppModalLoopForWindow(window);
  
  *canceledPtr = store->canceled;
  
  if (store->canceled)
  {
    //restore previous values on cancel
    store->bExportAudio = previousAudioExport;
    store->bExportVideo = previousVideoExport;
  }
  
bail:
  
  if (window)
  {
    if (portChanged)
    {
      QDSwapPort(savedPort, NULL);
    }
    
    DisposeWindow(window);
  }
  
  if (settingsWindowEventHandlerUPP)
    DisposeEventHandlerUPP(settingsWindowEventHandlerUPP);
  
  
  dbg_printf("[WebM] <   [%08lx] :: DoUserDialog() = 0x%04x\n", (UInt32) store, err);
  return err;
}



pascal ComponentResult WebMExportGetSettingsAsAtomContainer(WebMExportGlobalsPtr store, QTAtomContainer *settings)
{
  //QTAtom atom;
  QTAtomContainer ac = NULL;
  ComponentResult err;
  Boolean b_true = true;
  Boolean b_false = false;
  
  dbg_printf("[WebM -- %08lx] GetSettingsAsAtomContainer()\n", (UInt32) store);
  
  if (!settings)
    return paramErr;
  
  err = QTNewAtomContainer(&ac);
  
  if (err)
    goto bail;
  
  
  if (store->bExportVideo)
  {
    err = QTInsertChild(ac, kParentAtomIsContainer, kQTSettingsMovieExportEnableVideo,
                        1, 0, sizeof(b_true), &b_true, NULL);
  }
  else
  {
    err = QTInsertChild(ac, kParentAtomIsContainer, kQTSettingsMovieExportEnableVideo,
                        1, 0, sizeof(b_false), &b_false, NULL);
  }
  
  if (err)
    goto bail;
  
  if (store->bExportAudio)
  {
    err = QTInsertChild(ac, kParentAtomIsContainer, kQTSettingsMovieExportEnableSound,
                        1, 0, sizeof(b_true), &b_true, NULL);
  }
  else
  {
    err = QTInsertChild(ac, kParentAtomIsContainer, kQTSettingsMovieExportEnableSound,
                        1, 0, sizeof(b_false), &b_false, NULL);
  }
  
  if (err)
    goto bail;
  
  if (store->bExportVideo)
  {
    
    QTAtomContainer vs = NULL;
    err = noErr;
    
    if (store->videoSettingsAtom == NULL)
      getDefaultVP8Atom(store);
    
    vs = store->videoSettingsAtom;
    
    if (!err)
    {
      dbg_printf("[WebM %08lx] :: GetSettingsAsAtomContainer() = %ld %ld\n", (UInt32) store, err, GetHandleSize(vs));
      
      if (!err)
        err = QTInsertChildren(ac, kParentAtomIsContainer, vs);
      
    }
    
    if (err)
      goto bail;
  }
  
  if (store->bExportAudio)
  {
    QTAtomContainer as = NULL;
    err = noErr;
    
    if (store->audioSettingsAtom == NULL)
      err = getDefaultVorbisAtom(store);
    
    if (err) return err; //this may happen if no installed vorbis
    
    as = store->audioSettingsAtom;
    
    if (!err)
    {
      dbg_printf("[WebM] aAC [%08lx] :: GetSettingsAsAtomContainer() = %ld %ld\n", (UInt32) store, err, GetHandleSize(as));
      
      err = QTInsertChildren(ac, kParentAtomIsContainer, as);
    }
    
    if (err)
      goto bail;
  }
  
bail:
  
  if (err && ac)
  {
    QTDisposeAtomContainer(ac);
    ac = NULL;
  }
  
  *settings = ac;
  
  if (!err)
    dbg_dumpAtom(ac);
  
  dbg_printf("[WebM] <   [%08lx] :: GetSettingsAsAtomContainer() = %d [%ld]\n", (UInt32) store, err, settings != NULL ? GetHandleSize(*settings) : -1);
  return err;
}

pascal ComponentResult WebMExportSetSettingsFromAtomContainer(WebMExportGlobalsPtr store, QTAtomContainer settings)
{
  QTAtom atom;
  ComponentResult err = noErr;
  Boolean tmp;
  
  dbg_printf("[WebM]  >> [%08lx] :: SetSettingsFromAtomContainer([%ld])\n", (UInt32) store, settings != NULL ? GetHandleSize(settings) : -1);
  dbg_dumpAtom(settings);
  
  if (!settings)
    return paramErr;
  
  //store->bExportVideo = 1;
  atom = QTFindChildByID(settings, kParentAtomIsContainer,
                         kQTSettingsMovieExportEnableVideo, 1, NULL);
  
  if (atom)
  {
    err = QTCopyAtomDataToPtr(settings, atom, false, sizeof(tmp), &tmp, NULL);
    
    if (err)
      goto bail;
    
    store->bExportVideo = tmp;
  }
  
  atom = QTFindChildByID(settings, kParentAtomIsContainer,
                         kQTSettingsMovieExportEnableSound, 1, NULL);
  
  if (atom)
  {
    err = QTCopyAtomDataToPtr(settings, atom, false, sizeof(tmp), &tmp, NULL);
    
    if (err)
      goto bail;
    
    store->bExportAudio = tmp;
    dbg_printf("[webM] setsettingsFromAtomContainer store->bExportAudio = %d\n", store->bExportAudio);
  }
  
  atom = QTFindChildByID(settings, kParentAtomIsContainer, kQTSettingsVideo, 1, NULL);
  
  if (atom)
  {
    if (store->videoSettingsAtom)
    {
      QTDisposeAtomContainer(store->videoSettingsAtom);
      store->videoSettingsAtom = NULL;
    }
    
    err = QTCopyAtom(settings, atom, &store->videoSettingsAtom);
    
    if (err)
      goto bail;
    
  }
  
  atom = QTFindChildByID(settings, kParentAtomIsContainer, kQTSettingsSound, 1, NULL);
  
  if (atom)
  {
    if (store->audioSettingsAtom)
    {
      dbg_printf("[webM] SetSettingsFromAtomContainer set_a_settings NULL\n");
      QTDisposeAtomContainer(store->audioSettingsAtom);
      store->audioSettingsAtom = NULL;
    }
    
    err = QTCopyAtom(settings, atom, &store->audioSettingsAtom);
    
    if (err)
      goto bail;
  }
  
bail:
  dbg_printf("[WebM] <   [%08lx] :: SetSettingsFromAtomContainer() = %d\n", (UInt32) store, err);
  return err;
}


pascal ComponentResult WebMExportGetFileNameExtension(WebMExportGlobalsPtr store, OSType *extension)
{
  dbg_printf("[WebM -- %08lx] :: GetFileNameExtension()\n", (UInt32) store);
  *extension = 'webm';
  return noErr;
}

pascal ComponentResult WebMExportGetShortFileTypeString(WebMExportGlobalsPtr store, Str255 typeString)
{
  dbg_printf("[WebM %08lx] GetShortFileTypeString()\n", (UInt32) store);
  typeString[0] = '\x04';
  typeString[1] = 'W';
  typeString[2] = 'e';
  typeString[3] = 'b';
  typeString[4] = 'M';
  typeString[5] = '\x0';
  
  return noErr;
}

pascal ComponentResult WebMExportGetSourceMediaType(WebMExportGlobalsPtr store, OSType *mediaType)
{
  dbg_printf("[WebM %08lx] GetSourceMediaType()\n", (UInt32) store);
  
  if (!mediaType)
    return paramErr;
  
  *mediaType = 0; //any track type
  
  return noErr;
}


/* ========================================================================= */

static OSErr ConfigureQuickTimeMovieExporter(WebMExportGlobalsPtr store)
{
  QTAtomContainer    settings = NULL;
  OSErr              err;
  
  dbg_printf("[WebM %08lx] :: ConfigureQuickTimeMovieExporter()\n", (UInt32) store);
  
  err = MovieExportGetSettingsAsAtomContainer(store->self, &settings);
  
  if (!err)
  {
    /* quicktime movie exporter seems to have problems with 0.0/recommended sample rates in output -
     removing all the audio atoms for now */
    QTAtom atom = QTFindChildByID(settings, kParentAtomIsContainer, kQTSettingsSound, 1, NULL);
    
    if (atom)
    {
      QTRemoveAtom(settings, atom);
    }
    
    err = MovieExportSetSettingsFromAtomContainer(store->quickTimeMovieExporter, settings);
    
    if (!err)
    {
      CodecQ renderQuality = kRenderQuality_Medium;
      err = QTSetComponentProperty(store->quickTimeMovieExporter, kQTPropertyClass_SCAudio,
                                   kQTSCAudioPropertyID_RenderQuality,
                                   sizeof(UInt32), &renderQuality);
      err = noErr;
    }
  }
  
  if (settings)
    DisposeHandle(settings);
  
  dbg_printf("[WebM %08lx] :: ConfigureQuickTimeMovieExporter() = %ld\n", (UInt32) store, err);
  return err;
}


static void CloseAllStreams(WebMExportGlobalsPtr store)
{
  int i;
  
  if (store->streams != NULL)
  {
    for (i = 0; i < store->streamCount; i++)
    {
      GenericStream *gs = &(*store->streams)[i];
      
      if (gs->trackType == VideoMediaType)
      {
        VideoStreamPtr p = &gs->vid;
        
        if (p->decompressionSession != NULL)
        {
          ICMDecompressionSessionRelease(p->decompressionSession);
          p->decompressionSession = NULL;
        }
        
        if (p->compressionSession != NULL)
        {
          ICMCompressionSessionRelease(p->compressionSession);
          p->compressionSession = NULL;
        }
        
      }
      else if (gs->trackType == SoundMediaType)
      {
        if (gs->aud.vorbisComponentInstance != NULL)
          CloseComponent(gs->aud.vorbisComponentInstance);
        
      }
      
      freeFrameQueue(&gs->frameQueue);
    }
    
    DisposeHandle((Handle) store->streams);
  }
  
  store->streamCount  = 0;
  store->streams = NULL;
}

#define kCharacteristicHasVideoFrameRate FOUR_CHAR_CODE('vfrr')
static ComponentResult _getFrameRate(Movie theMovie, double *fps)
{
  Track videoTrack = GetMovieIndTrackType(theMovie, 1, kCharacteristicHasVideoFrameRate,
                                          movieTrackCharacteristic | movieTrackEnabledOnly);
  Media media = GetTrackMedia(videoTrack);
  long sampleCount = GetMediaSampleCount(media);
  TimeValue64 duration = GetMediaDisplayDuration(media);
  TimeValue64 timeScale = GetMediaTimeScale(media);
  dbg_printf("[WebM] computing framerate %ld * %lld / %lld \n",
             sampleCount, timeScale, duration);
  *fps = sampleCount * 1.0 *  timeScale /  duration;
  return noErr;
}

