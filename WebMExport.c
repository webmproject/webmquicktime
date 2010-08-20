// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#if defined(__APPLE_CC__)
#include <QuickTime/QuickTime.h>
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
pascal ComponentResult WebMExportGetComponentPropertyInfo(WebMExportGlobalsPtr   globals,
        ComponentPropertyClass inPropClass,
        ComponentPropertyID    inPropID,
        ComponentValueType     *outPropType,
        ByteCount              *outPropValueSize,
        UInt32                 *outPropertyFlags);
pascal ComponentResult WebMExportGetComponentProperty(WebMExportGlobalsPtr  globals,
        ComponentPropertyClass inPropClass,
        ComponentPropertyID    inPropID,
        ByteCount              inPropValueSize,
        ComponentValuePtr      outPropValueAddress,
        ByteCount              *outPropValueSizeUsed);
pascal ComponentResult WebMExportSetComponentProperty(WebMExportGlobalsPtr  globals,
        ComponentPropertyClass inPropClass,
        ComponentPropertyID    inPropID,
        ByteCount              inPropValueSize,
        ConstComponentValuePtr inPropValueAddress);



pascal ComponentResult WebMExportDoUserDialog(WebMExportGlobalsPtr globals, Movie theMovie, Track onlyThisTrack,
        TimeValue startTime, TimeValue duration, Boolean *canceledPtr);


pascal ComponentResult WebMExportGetSettingsAsAtomContainer(WebMExportGlobalsPtr globals, QTAtomContainer *settings);
pascal ComponentResult WebMExportSetSettingsFromAtomContainer(WebMExportGlobalsPtr globals, QTAtomContainer settings);
pascal ComponentResult WebMExportGetFileNameExtension(WebMExportGlobalsPtr globals, OSType *extension);
pascal ComponentResult WebMExportGetShortFileTypeString(WebMExportGlobalsPtr globals, Str255 typeString);
pascal ComponentResult WebMExportGetSourceMediaType(WebMExportGlobalsPtr globals, OSType *mediaType);



static void CloseAllStreams(WebMExportGlobalsPtr globals);

static OSErr ConfigureQuickTimeMovieExporter(WebMExportGlobalsPtr globals);

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


pascal ComponentResult WebMExportOpen(WebMExportGlobalsPtr globals, ComponentInstance self)
{
    ComponentDescription cd;
    ComponentResult err;

    dbg_printf("[WebM]  >> [%08lx] :: Open()\n", (UInt32) globals);

    globals = (WebMExportGlobalsPtr) NewPtrClear(sizeof(WebMExportGlobals));
    err = MemError();

    if (!err)
    {
        globals->self = self;
        globals->framerate = 0;

        globals->bExportVideo = 1;
        globals->bExportAudio = 1;

        globals->audioSettingsAtom = NULL;
        globals->videoSettingsAtom = NULL;
        globals->streams = NULL;
        globals->streamCount = 0;
        globals->cueHandle = NULL;
        globals->cueCount = 0;

        memset(&globals->audioBSD, 0, sizeof(AudioStreamBasicDescription));

        globals->audioBSD.mFormatID = kAudioFormatXiphVorbis;
        globals->audioBSD.mChannelsPerFrame = 1;
        globals->audioBSD.mSampleRate = 44100.000;

        globals->bMovieHasAudio = true;
        globals->bMovieHasVideo = true;

        globals->webmTimeCodeScale = 1000000; ///TODO figure out about how to use this

        SetComponentInstanceStorage(self, (Handle) globals);

        // Get the QuickTime Movie export component
        // Because we use the QuickTime Movie export component, search for
        // the 'MooV' exporter using the following ComponentDescription values
        cd.componentType = MovieExportType;
        cd.componentSubType = kQTFileTypeMovie;
        cd.componentManufacturer = kAppleManufacturer;
        cd.componentFlags = canMovieExportFromProcedures | movieExportMustGetSourceMediaType;
        cd.componentFlagsMask = cd.componentFlags;

        err = OpenAComponent(FindNextComponent(NULL, &cd), &globals->quickTimeMovieExporter);
    }

    dbg_printf("[WebM] <   [%08lx] :: Open()\n", (UInt32) globals);
    return err;
}


pascal ComponentResult WebMExportClose(WebMExportGlobalsPtr globals, ComponentInstance self)
{
    dbg_printf("[WebM]  >> [%08lx] :: Close()\n", (UInt32) globals);

    if (globals)
    {
        if (globals->quickTimeMovieExporter)
            CloseComponent(globals->quickTimeMovieExporter);

        CloseAllStreams(globals);

        if (globals->videoSettingsAtom)
            QTDisposeAtomContainer(globals->videoSettingsAtom);

        if (globals->audioSettingsAtom)
            QTDisposeAtomContainer(globals->audioSettingsAtom);


        DisposePtr((Ptr) globals);
    }

    dbg_printf("[WebM] <   [%08lx] :: Close()\n", (UInt32) globals);
    return noErr;
}

pascal ComponentResult WebMExportVersion(WebMExportGlobalsPtr globals)
{
#pragma unused(globals)
    dbg_printf("[WebM]  >> [%08lx] :: Version()\n", (UInt32) globals);
    dbg_printf("[WebM] <   [%08lx] :: Version()\n", (UInt32) globals);
    return kMkv_spit__Version;
}

pascal ComponentResult WebMExportGetComponentPropertyInfo(WebMExportGlobalsPtr   globals,
        ComponentPropertyClass inPropClass,
        ComponentPropertyID    inPropID,
        ComponentValueType     *outPropType,
        ByteCount              *outPropValueSize,
        UInt32                 *outPropertyFlags)
{
    ComponentResult err = noErr;
    dbg_printf("[WebM]  >> [%08lx] :: GetComponentPropertyInfo('%4.4s', '%4.4s')\n", (UInt32) globals, (char *) &inPropClass, (char *) &inPropID);
    dbg_printf("[WebM] <   [%08lx] :: GetComponentPropertyInfo() = %ld\n", (UInt32) globals, err);
    return err;
}

pascal ComponentResult WebMExportGetComponentProperty(WebMExportGlobalsPtr  globals,
        ComponentPropertyClass inPropClass,
        ComponentPropertyID    inPropID,
        ByteCount              inPropValueSize,
        ComponentValuePtr      outPropValueAddress,
        ByteCount              *outPropValueSizeUsed)
{
    dbg_printf("[WebM]  >> [%08lx] :: GetComponentProperty('%4.4s', '%4.4s', %ld)\n", (UInt32) globals, (char *) &inPropClass, (char *) &inPropID, inPropValueSize);
    dbg_printf("[WebM] <   [%08lx] :: GetComponentProperty()\n", (UInt32) globals);
    return noErr;
}

pascal ComponentResult WebMExportSetComponentProperty(WebMExportGlobalsPtr  globals,
        ComponentPropertyClass inPropClass,
        ComponentPropertyID    inPropID,
        ByteCount              inPropValueSize,
        ConstComponentValuePtr inPropValueAddress)
{
    ComponentResult err = noErr;
    dbg_printf("[WebM]  >> [%08lx] :: SetComponentProperty('%4.4s', '%4.4s', %ld)\n", (UInt32) globals, (char *) &inPropClass, (char *) &inPropID, inPropValueSize);
    //just pass on the property
    err = QTSetComponentProperty(globals->quickTimeMovieExporter, inPropClass,
                                 inPropID, inPropValueSize, inPropValueAddress);


    dbg_printf("[WebM] <   [%08lx] :: SetComponentProperty() = %ld\n", (UInt32) globals, err);
    return err;
}

pascal ComponentResult WebMExportValidate(WebMExportGlobalsPtr globals, Movie theMovie, Track onlyThisTrack, Boolean *valid)
{
    OSErr err;

    dbg_printf("[WebM]  >> [%08lx] :: Validate()\n", (UInt32) globals);

    // The QT movie export component must be cool with this before we can be
    err = MovieExportValidate(globals->quickTimeMovieExporter, theMovie, onlyThisTrack, valid);

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

    dbg_printf("[WebM] <   [%08lx] :: Validate() = %d, %d\n", (UInt32) globals, err, *valid);
    return err;
}



pascal ComponentResult WebMExportToFile(WebMExportGlobalsPtr globals, const FSSpec *theFilePtr,
                                        Movie theMovie, Track onlyThisTrack, TimeValue startTime,
                                        TimeValue duration)
{
    AliasHandle alias;
    ComponentResult err;

    dbg_printf("[WebM]  >> [%08lx] :: ToFile(%d, %ld, %ld)\n", (UInt32) globals, onlyThisTrack != NULL, startTime, duration);

    err = QTNewAlias(theFilePtr, &alias, true);

    if (!err)
    {
        err = MovieExportToDataRef(globals->self, (Handle) alias, rAliasType, theMovie, onlyThisTrack, startTime, duration);

        DisposeHandle((Handle) alias);
    }

    dbg_printf("[WebM] <   [%08lx] :: ToFile()\n", (UInt32) globals);
    return err;
}

pascal ComponentResult WebMExportToDataRef(WebMExportGlobalsPtr globals, Handle dataRef, OSType dataRefType,
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

    dbg_printf("[WebM]  >> [%08lx] :: ToDataRef(%d, %ld, %ld)\n", (UInt32) globals, onlyThisTrack != NULL, startTime, duration);
    dbg_printf("[WebM] ToDataRef -- bMovieHasAudio %d, bMovieHasVideo %d, bExportAudio %d, bExportVideo %d\n",
               globals->bMovieHasAudio, globals->bMovieHasVideo, globals->bExportAudio, globals->bExportVideo);
    // TODO: loop all tracks

    if (globals->bExportVideo && globals->bMovieHasVideo)
    {
        err = MovieExportNewGetDataAndPropertiesProcs(globals->quickTimeMovieExporter, VideoMediaType, &scale, theMovie,
                onlyThisTrack, startTime, duration, &getVideoPropertyProc,
                &getVideoDataProc, &videoRefCon);
        dbg_printf("[WebM]   # [%08lx] :: ToDataRef() = %ld\n", (UInt32) globals, err);

        if (!err)
        {
            err = MovieExportAddDataSource(globals->self, VideoMediaType, scale, &trackID, getVideoPropertyProc, getVideoDataProc, videoRefCon);

            if (!err)
                have_sources = true;
        }

        if (globals->framerate == 0)
            _getFrameRate(theMovie, &globals->framerate);
    }

    if (globals->bExportAudio && globals->bMovieHasAudio)
    {
        err = MovieExportNewGetDataAndPropertiesProcs(globals->quickTimeMovieExporter, SoundMediaType, &scale, theMovie,
                onlyThisTrack, startTime, duration, &getSoundPropertyProc,
                &getSoundDataProc, &audioRefCon);

        dbg_printf("[WebM]   = [%08lx] :: ToDataRef() = %ld\n", (UInt32) globals, err);

        if (!err)
        {
            // ** Add the audio data source **
            err = MovieExportAddDataSource(globals->self, SoundMediaType, scale, &trackID, getSoundPropertyProc, getSoundDataProc, audioRefCon);

            if (!err)
                have_sources = true;
        }
    }

    if (have_sources)
    {
        err = MovieExportFromProceduresToDataRef(globals->self, dataRef, dataRefType);
    }
    else
    {
        err = invalidMovie;
    }

    if (getSoundPropertyProc || getSoundDataProc)
        MovieExportDisposeGetDataAndPropertiesProcs(globals->quickTimeMovieExporter, getSoundPropertyProc, getSoundDataProc, audioRefCon);

    if (getVideoPropertyProc || getVideoDataProc)
        MovieExportDisposeGetDataAndPropertiesProcs(globals->quickTimeMovieExporter, getVideoPropertyProc, getVideoDataProc, videoRefCon);

    dbg_printf("[WebM] <   [%08lx] :: ToDataRef() = %d, %ld\n", (UInt32) globals, err, trackID);
    return err;
}

pascal ComponentResult WebMExportFromProceduresToDataRef(WebMExportGlobalsPtr globals, Handle dataRef, OSType dataRefType)
{
    DataHandler    dataH = NULL;
    ComponentResult err;

    dbg_printf("[WebM--%08lx] :: FromProceduresToDataRef()\n", (UInt32) globals);

    if (globals->streamCount == 0)
        return noErr;  //no data to write

    if (!dataRef || !dataRefType)
        return paramErr;

    // Get and open a Data Handler Component that can write to the dataRef
    err = OpenAComponent(GetDataHandler(dataRef, dataRefType, kDataHCanWrite), &dataH);

    if (err)
        goto bail;

    DataHSetDataRef(dataH, dataRef);

    err = DataHCreateFile(dataH, FOUR_CHAR_CODE('TVOD'), true);

    if (err)
        goto bail;

    DataHSetMacOSFileType(dataH, FOUR_CHAR_CODE('webm'));

    err = DataHOpenForWrite(dataH);

    if (err)
        goto bail;

    err = ConfigureQuickTimeMovieExporter(globals);

    if (err)
        goto bail;

    err = muxStreams(globals, dataH);

bail:

    if (dataH)
        CloseComponent(dataH);

    dbg_printf("[WebM] <   [%08lx] :: FromProceduresToDataRef() = %ld\n", (UInt32) globals, err);
    return err;
}



pascal ComponentResult WebMExportNewGetDataAndPropertiesProcs(WebMExportGlobalsPtr globals, OSType trackType, TimeScale *scale, Movie theMovie,
        Track theTrack, TimeValue startTime, TimeValue duration,
        MovieExportGetPropertyUPP *propertyProc, MovieExportGetDataUPP *getDataProc,
        void **refCon)
{
    ComponentResult err;
    dbg_printf("[WebM]  >> [%08lx] :: NewGetDataAndPropertiesProcs(%4.4s, %ld, %ld)\n", (UInt32) globals, (char *) &trackType, startTime, duration);

    err = MovieExportNewGetDataAndPropertiesProcs(globals->quickTimeMovieExporter, trackType, scale, theMovie, theTrack, startTime, duration,
            propertyProc, getDataProc, refCon);

    dbg_printf("[WebM] <   [%08lx] :: NewGetDataAndPropertiesProcs() = %ld (movieGet.refCon = %08lx)\n",
               (UInt32) globals, err, (UInt32)(refCon != NULL ? *refCon : NULL));
    return err;
}

pascal ComponentResult WebMExportDisposeGetDataAndPropertiesProcs(WebMExportGlobalsPtr globals,
        MovieExportGetPropertyUPP propertyProc, MovieExportGetDataUPP getDataProc,
        void *refCon)
{
    ComponentResult err;
    dbg_printf("[WebM--%08lx] :: DisposeGetDataAndPropertiesProcs(%08lx)\n", (UInt32) globals, (UInt32) refCon);
    err = MovieExportDisposeGetDataAndPropertiesProcs(globals->quickTimeMovieExporter, propertyProc, getDataProc, refCon);
    return err;
}

static void _addNewStream(WebMExportGlobalsPtr globals)
{
    globals->streamCount ++;

    if (globals->streams)
        SetHandleSize((Handle) globals->streams, sizeof(GenericStream) * globals->streamCount);
    else
        globals->streams = (GenericStream **) NewHandleClear(sizeof(GenericStream));

    dbg_printf("[webm] adding stream %d size is %ld\n", globals->streamCount, sizeof(GenericStream) * globals->streamCount);
}

pascal ComponentResult WebMExportAddDataSource(WebMExportGlobalsPtr globals, OSType trackType, TimeScale scale,
        long *trackIDPtr, MovieExportGetPropertyUPP propertyProc,
        MovieExportGetDataUPP getDataProc, void *refCon)
{
    ComponentResult err = noErr;

    dbg_printf("[WebM - %08lx] :: AddDataSource('%4.4s')\n", (UInt32) globals, (char *) &trackType);

    *trackIDPtr = 0;

    if (!scale || !trackType || !getDataProc || !propertyProc)
        return paramErr;

    if (trackType == VideoMediaType || trackType == SoundMediaType)
    {
        _addNewStream(globals);
        *trackIDPtr = globals->streamCount;

        GenericStream *gs = &(*globals->streams)[globals->streamCount-1];
        gs->trackType = trackType;
        StreamSource *source = NULL;

        if (trackType == VideoMediaType)
        {
            VideoStreamPtr p = &gs->vid;
            initVideoStream(p);
            source = &p->source;
        }
        else if (trackType == SoundMediaType)
        {
            AudioStreamPtr p = &gs->aud;
            initAudioStream(p);
            source = &p->source;
        }

        initStreamSource(source, scale, *trackIDPtr,  propertyProc,
                         getDataProc, refCon);
    }
    else
    {
        dbg_printf("[WebM] ignoring stream %d\n", trackType);
    }

    return err;
}

pascal ComponentResult WebMExportSetProgressProc(WebMExportGlobalsPtr globals, MovieProgressUPP proc, long refCon)
{
    dbg_printf("[WebM - %08lx] :: SetProgressProc()\n", (UInt32) globals);

    globals->progressProc = proc;
    globals->progressRefCon = refCon;
    return noErr;
}



pascal ComponentResult WebMExportDoUserDialog(WebMExportGlobalsPtr globals, Movie theMovie, Track onlyThisTrack,
        TimeValue startTime, TimeValue duration, Boolean *canceledPtr)
{
    WindowRef   window = NULL;
    Boolean     portChanged = false;

    CGrafPtr    savedPort;
    OSErr       err = resFNotFound;
    Boolean previousAudioExport = globals->bExportAudio, previousVideoExport = globals->bExportVideo;

    EventTypeSpec eventList[] = {{kEventClassCommand, kEventCommandProcess}};
    EventHandlerUPP settingsWindowEventHandlerUPP = NewEventHandlerUPP(SettingsWindowEventHandler);

    dbg_printf("[WebM %08lx] DoUserDialog()\n", (UInt32) globals);
    getWindow(&window);

    portChanged = QDSwapPort(GetWindowPort(window), &savedPort);

    *canceledPtr = false;

    err = checkMovieHasVideoAudio(globals, theMovie, onlyThisTrack);


    dbg_printf("[WebM] DoUserDialog() End theMovie Block -- allow Aud %d, allow Vid %d, aud disable %d, vid disable %d\n",
    globals->bMovieHasAudio, globals->bMovieHasVideo, globals->bExportAudio, globals->bExportVideo);
    enableDisableControls(globals, window);

    globals->setdlg_movie = theMovie;
    globals->setdlg_track = onlyThisTrack;

    InstallWindowEventHandler(window, settingsWindowEventHandlerUPP, GetEventTypeCount(eventList), eventList, globals, NULL);

    ShowWindow(window);

    RunAppModalLoopForWindow(window);

    *canceledPtr = globals->canceled;

    if (globals->canceled)
    {
        //restore previous values on cancel
        globals->bExportAudio = previousAudioExport;
        globals->bExportVideo = previousVideoExport;
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


    dbg_printf("[WebM] <   [%08lx] :: DoUserDialog() = 0x%04x\n", (UInt32) globals, err);
    return err;
}



pascal ComponentResult WebMExportGetSettingsAsAtomContainer(WebMExportGlobalsPtr globals, QTAtomContainer *settings)
{
    //QTAtom atom;
    QTAtomContainer ac = NULL;
    ComponentResult err;
    Boolean b_true = true;
    Boolean b_false = false;

    dbg_printf("[WebM]  [%08lx] GetSettingsAsAtomContainer()\n", (UInt32) globals);

    if (!settings)
        return paramErr;

    err = QTNewAtomContainer(&ac);

    if (err)
        goto bail;


    if (globals->bExportVideo)
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

    if (globals->bExportAudio)
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

    if (globals->bExportVideo)
    {

        QTAtomContainer vs = NULL;
        err = noErr;

        if (globals->videoSettingsAtom == NULL)
            getDefaultVP8Atom(globals);

        vs = globals->videoSettingsAtom;

        if (!err)
        {
            //err = _video_settings_to_ac(globals, &vs);
            dbg_printf("[WebM] vAC [%08lx] :: GetSettingsAsAtomContainer() = %ld %ld\n", (UInt32) globals, err, GetHandleSize(vs));

            if (!err)
                err = QTInsertChildren(ac, kParentAtomIsContainer, vs);

        }

        if (err)
            goto bail;
    }

    if (globals->bExportAudio)
    {
        QTAtomContainer as = NULL;
        err = noErr;

        if (globals->audioSettingsAtom == NULL)
            err = getDefaultVorbisAtom(globals);

        if (err) return err; //this may happen if no installed vorbis

        as = globals->audioSettingsAtom;

        if (!err)
        {
            dbg_printf("[WebM] aAC [%08lx] :: GetSettingsAsAtomContainer() = %ld %ld\n", (UInt32) globals, err, GetHandleSize(as));

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

    dbg_printf("[WebM] <   [%08lx] :: GetSettingsAsAtomContainer() = %d [%ld]\n", (UInt32) globals, err, settings != NULL ? GetHandleSize(*settings) : -1);
    return err;
}

pascal ComponentResult WebMExportSetSettingsFromAtomContainer(WebMExportGlobalsPtr globals, QTAtomContainer settings)
{
    QTAtom atom;
    ComponentResult err = noErr;
    Boolean tmp;

    dbg_printf("[WebM]  >> [%08lx] :: SetSettingsFromAtomContainer([%ld])\n", (UInt32) globals, settings != NULL ? GetHandleSize(settings) : -1);
    dbg_dumpAtom(settings);

    if (!settings)
        return paramErr;

    //globals->bExportVideo = 1;
    atom = QTFindChildByID(settings, kParentAtomIsContainer,
    kQTSettingsMovieExportEnableVideo, 1, NULL);

    if (atom)
    {
        err = QTCopyAtomDataToPtr(settings, atom, false, sizeof(tmp), &tmp, NULL);

        if (err)
            goto bail;

        globals->bExportVideo = tmp;
    }

    atom = QTFindChildByID(settings, kParentAtomIsContainer,
    kQTSettingsMovieExportEnableSound, 1, NULL);

    if (atom)
    {
        err = QTCopyAtomDataToPtr(settings, atom, false, sizeof(tmp), &tmp, NULL);

        if (err)
            goto bail;

        globals->bExportAudio = tmp;
        dbg_printf("[webM] setsettingsFromAtomContainer globals->bExportAudio = %d\n", globals->bExportAudio);
    }

    atom = QTFindChildByID(settings, kParentAtomIsContainer, kQTSettingsVideo, 1, NULL);

    if (atom)
    {
        if (globals->videoSettingsAtom)
        {
            QTDisposeAtomContainer(globals->videoSettingsAtom);
            globals->videoSettingsAtom = NULL;
        }

        err = QTCopyAtom(settings, atom, &globals->videoSettingsAtom);

        if (err)
            goto bail;

    }

    atom = QTFindChildByID(settings, kParentAtomIsContainer, kQTSettingsSound, 1, NULL);

    if (atom)
    {
        if (globals->audioSettingsAtom)
        {
            dbg_printf("[webM] SetSettingsFromAtomContainer set_a_settings NULL\n");
            QTDisposeAtomContainer(globals->audioSettingsAtom);
            globals->audioSettingsAtom = NULL;
        }

        err = QTCopyAtom(settings, atom, &globals->audioSettingsAtom);

        if (err)
            goto bail;
    }

bail:
    dbg_printf("[WebM] <   [%08lx] :: SetSettingsFromAtomContainer() = %d\n", (UInt32) globals, err);
    return err;
}


pascal ComponentResult WebMExportGetFileNameExtension(WebMExportGlobalsPtr globals, OSType *extension)
{
#pragma unused(globals)
    dbg_printf("[WebM]  >> [%08lx] :: GetFileNameExtension()\n", (UInt32) globals);
    *extension = 'webm';
    dbg_printf("[WebM] <   [%08lx] :: GetFileNameExtension()\n", (UInt32) globals);
    return noErr;
}

pascal ComponentResult WebMExportGetShortFileTypeString(WebMExportGlobalsPtr globals, Str255 typeString)
{
    dbg_printf("[WebM %08lx] GetShortFileTypeString()\n", (UInt32) globals);
    typeString[0] = '\x04';
    typeString[1] = 'W';
    typeString[2] = 'e';
    typeString[3] = 'b';
    typeString[4] = 'M';
    typeString[5] = '\x0';

    return noErr;
}

pascal ComponentResult WebMExportGetSourceMediaType(WebMExportGlobalsPtr globals, OSType *mediaType)
{
    dbg_printf("[WebM %08lx] GetSourceMediaType()\n", (UInt32) globals);

    if (!mediaType)
        return paramErr;

    *mediaType = 0; //any track type

    return noErr;
}


/* ========================================================================= */

static OSErr ConfigureQuickTimeMovieExporter(WebMExportGlobalsPtr globals)
{
    QTAtomContainer    settings = NULL;
    OSErr              err;

    dbg_printf("[WebM %08lx] :: ConfigureQuickTimeMovieExporter()\n", (UInt32) globals);

    err = MovieExportGetSettingsAsAtomContainer(globals->self, &settings);
    dbg_printf("[WebM]  gO [%08lx] :: ConfigureQuickTimeMovieExporter() = %ld\n", (UInt32) globals, err);

    if (!err)
    {
        /* quicktime movie exporter seems to have problems with 0.0/recommended sample rates in output -
           removing all the audio atoms for now */
        QTAtom atom = QTFindChildByID(settings, kParentAtomIsContainer, kQTSettingsSound, 1, NULL);

        if (atom)
        {
            QTRemoveAtom(settings, atom);
        }

        err = MovieExportSetSettingsFromAtomContainer(globals->quickTimeMovieExporter, settings);
        dbg_printf("[WebM]  sE [%08lx] :: ConfigureQuickTimeMovieExporter() = %ld\n", (UInt32) globals, err);

        if (!err)
        {
            CodecQ renderQuality = kRenderQuality_Medium;
            err = QTSetComponentProperty(globals->quickTimeMovieExporter, kQTPropertyClass_SCAudio,
            kQTSCAudioPropertyID_RenderQuality,
            sizeof(UInt32), &renderQuality);
            err = noErr;
        }
    }

    if (settings)
        DisposeHandle(settings);

    dbg_printf("[WebM] <   [%08lx] :: ConfigureQuickTimeMovieExporter() = %ld\n", (UInt32) globals, err);
    return err;
}


static void CloseAllStreams(WebMExportGlobalsPtr globals)
{
    int i;

    if (globals->streams != NULL)
    {
        for (i = 0; i < globals->streamCount; i++)
        {
            GenericStream *gs = &(*globals->streams)[i];
            WebMBuffer *buf;

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

                buf = &p->outBuf;
            }
            else if (gs->trackType == SoundMediaType)
                buf = &gs->aud.outBuf;

            freeBuffer(buf);
        }

        DisposeHandle((Handle) globals->streams);
    }

    globals->streamCount  = 0;
    globals->streams = NULL;
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
    *fps = (double)sampleCount * (double) timeScale / (double) duration;
    return noErr;
}

