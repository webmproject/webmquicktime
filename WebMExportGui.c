// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include "WebMExportStructs.h"
#include "WebMExportGui.h"

#if defined(__APPLE_CC__)
#include <QuickTime/QuickTime.h>
#else
#include <QuickTimeComponents.h>
#endif

#include "log.h"
#include "WebMExportVersions.h"

ComponentResult getWindow(WindowRef *window)
{
    ComponentResult err = noErr;
    CFBundleRef bundle = NULL;
    IBNibRef    nibRef = NULL;

    bundle = CFBundleGetBundleWithIdentifier(CFSTR(kWebMExportBundleID));

    if (bundle == NULL)
    {
        dbg_printf("[WebM]  Error :: CFBundleGetBundleWithIdentifier\n");
        err = readErr;
        goto bail;
    }

    err = CreateNibReferenceWithCFBundle(bundle, CFSTR("WebMExport"), &nibRef);
    if (err)
    {
        dbg_printf("[WebM]  >> Error :: CreateNibReferenceWithCFBundle\n");
        goto bail;
    }

    err = CreateWindowFromNib(nibRef, CFSTR("Settings"), window);

    if (err)
    {
        dbg_printf("[WebM]  >> Error :: CreateWindowFromNib\n");
        goto bail;
    }

bail:

    if (nibRef)
        DisposeNibReference(nibRef);


    return err;
}

static void _EnableControl(ControlRef *id, Boolean enable)
{
    if (enable)
        EnableControl(*id);
    else
        DisableControl(*id);
}

ComponentResult enableDisableControls(WebMExportGlobalsPtr globals, WindowRef window)
{
    ControlRef  checkBoxControl, settingsControl;
    ControlID   audioCheckboxID = {'WebM', 6}, videoCheckboxID = {'WebM', 5};
    ControlID   audioSettingsID = {'WebM', 4}, videoSettingsID = {'WebM', 3};
    ControlID   twoPassID = {'WebM', 7}, metaDataID = {'WebM', 8};

    dbg_printf("[WebM] enableDisableControls -- bMovieHasAudio %d, bMovieHasVideo %d, bExportAudio %d, bExportVideo %d\n",
               globals->bMovieHasAudio, globals->bMovieHasVideo, globals->bExportAudio, globals->bExportVideo);

    GetControlByID(window, &videoCheckboxID, &checkBoxControl);
    GetControlByID(window, &videoSettingsID, &settingsControl);

    SetControl32BitValue(checkBoxControl, globals->bExportVideo && globals->bMovieHasVideo);
    _EnableControl(&checkBoxControl, globals->bMovieHasVideo);
    _EnableControl(&settingsControl, globals->bExportVideo && globals->bMovieHasVideo);

    GetControlByID(window, &audioCheckboxID, &checkBoxControl);
    GetControlByID(window, &audioSettingsID, &settingsControl);

    SetControl32BitValue(checkBoxControl, globals->bExportAudio && globals->bMovieHasAudio);
    _EnableControl(&checkBoxControl, globals->bMovieHasAudio);
    _EnableControl(&settingsControl, globals->bExportAudio && globals->bMovieHasAudio);
    
    GetControlByID(window, &twoPassID, &checkBoxControl);
    _EnableControl(&checkBoxControl, globals->bExportVideo && globals->bMovieHasVideo);

}

ComponentResult checkMovieHasVideoAudio(WebMExportGlobalsPtr globals, Movie theMovie, Track onlyThisTrack)
{
    if (theMovie == NULL)
        return;

    if (onlyThisTrack == NULL)
    {
        Track vt = GetMovieIndTrackType(theMovie, 1, VisualMediaCharacteristic, movieTrackCharacteristic | movieTrackEnabledOnly);
        globals->bMovieHasVideo = vt != NULL;

        Track at = GetMovieIndTrackType(theMovie, 1, AudioMediaCharacteristic, movieTrackCharacteristic | movieTrackEnabledOnly);
        globals->bMovieHasAudio = at != NULL;
    }
    else
    {
        MediaHandler mh = GetMediaHandler(GetTrackMedia(onlyThisTrack));
        Boolean has_char = false;

        MediaHasCharacteristic(mh, VisualMediaCharacteristic, &globals->bMovieHasVideo);
        MediaHasCharacteristic(mh, AudioMediaCharacteristic, &globals->bMovieHasAudio);
    }

    return noErr;
}


pascal OSStatus SettingsWindowEventHandler(EventHandlerCallRef inHandler, EventRef inEvent, void *inUserData)
{
    WindowRef window = NULL;
    HICommand command;
    OSStatus rval = eventNotHandledErr;
    WebMExportGlobalsPtr globals = (WebMExportGlobalsPtr) inUserData;

    dbg_printf("[WebM]  >> [%08lx] :: SettingsWindowEventHandler()\n", (UInt32) globals);

    window = ActiveNonFloatingWindow();

    if (window == NULL)
        goto bail;

    GetEventParameter(inEvent, kEventParamDirectObject, typeHICommand, NULL, sizeof(HICommand), NULL, &command);

    dbg_printf("[WebM]   | [%08lx] :: SettingsWindowEventHandler('%4.4s')\n", (UInt32) globals, (char *) &command.commandID);

    switch (command.commandID)
    {
    case kHICommandOK:
        globals->canceled = false;
        QuitAppModalLoopForWindow(window);
        rval = noErr;
        break;

    case kHICommandCancel:
        globals->canceled = true;
        QuitAppModalLoopForWindow(window);
        rval = noErr;
        break;

    case 'WMvs':                //Video Settings
        rval = OpenVP8Dlg(globals, window);

        if (rval == userCanceledErr || rval == scUserCancelled)
            rval = noErr; // User cancelling is ok.

        break;

    case 'WMas':                // Audio Settings
        rval = OpenVorbisDlg(globals, window);

        if (rval == userCanceledErr || rval == scUserCancelled)
            rval = noErr;

        break;

    case 'WMac':                //Enable Audio CheckBoxk
        globals->bExportAudio = !globals->bExportAudio;
        enableDisableControls(globals, window);
        rval = noErr;
        break;

    case 'WMvc':               //Enable Video CheckBoxk
        globals->bExportVideo = !globals->bExportVideo;
        enableDisableControls(globals, window);
        rval = noErr;
        break;
            
    default:
        break;
    }

bail:
    dbg_printf("[WebM] <   [%08lx] :: SettingsWindowEventHandler() = 0x%08lx\n", (UInt32) globals, rval);
    return rval;
}

