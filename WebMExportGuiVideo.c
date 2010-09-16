// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include "WebMExportStructs.h"

#if defined(__APPLE_CC__)
#include <QuickTime/QuickTime.h>
#else
#include <QuickTimeComponents.h>
#endif

#include "log.h"
#include "WebMExportVersions.h"

#include "WebMExportGuiVideo.h"



ComponentResult getDefaultVP8Atom(WebMExportGlobalsPtr globals)
{
    ComponentResult err = noErr;
    dbg_printf("[WebM - %08lx] getDefaultVP8Atom()\n", (UInt32) globals);
    ComponentInstance stdVideo = NULL;

    err = OpenADefaultComponent(StandardCompressionType, StandardCompressionSubType, &stdVideo);

    dbg_printf("[WebM] getDefaultVP8Atom()\n", (UInt32) globals);

    long sc_prefs = scAllowZeroFrameRate | scAllowZeroKeyFrameRate | scShowDataRateAsKilobits;
    err = SCSetInfo(stdVideo, scPreferenceFlagsType, &sc_prefs);

    if (err) goto bail;

    SCSpatialSettings ss = {kVP8CodecFormatType, (CodecComponent) kVP8CodecFormatType,
                            0, codecNormalQuality
                           };

    SCTemporalSettings ts = {codecNormalQuality, 0, 64};
    ts.frameRate = FloatToFixed(globals->framerate);
    SCDataRateSettings ds = {0, 0, 0, 0};

    err = SCSetInfo(stdVideo, scSpatialSettingsType, &ss);

    if (err) goto bail;

    err = SCSetInfo(stdVideo, scTemporalSettingsType, &ts);

    if (err) goto bail;

    err = SCSetInfo(stdVideo, scDataRateSettingsType, &ds);

    if (err) goto bail;

bail:
    dbg_printf("----------- %08lx\n" , (UInt32)stdVideo);

    if (stdVideo != NULL)
    {
        //here we try to get settings even if there was an error earlier
        err = SCGetSettingsAsAtomContainer(stdVideo, &globals->videoSettingsAtom);
        dbg_dumpAtom(globals->videoSettingsAtom);
        CloseComponent(stdVideo);
    }

    return err;
}

//TODO don't think WindowRef is needed or used.
ComponentResult OpenVP8Dlg(WebMExportGlobalsPtr globals, WindowRef window)
{
    ComponentResult err = noErr;

    ComponentInstance stdVideo = NULL;
    PicHandle currentPictureHandle = NULL;

    //    SCExtendedProcs xProcs;

    dbg_printf("[WebM]  >> [%08lx] :: OpenVP8Dlg()\n", (UInt32) globals);

    if (globals->videoSettingsAtom == NULL)
        getDefaultVP8Atom(globals);

    err = OpenADefaultComponent(StandardCompressionType, StandardCompressionSubType, &stdVideo);

    if (err) goto bail;

    err = SCSetSettingsFromAtomContainer(stdVideo, globals->videoSettingsAtom);

    if (err) goto bail;

    Handle compressionList = NULL;
    compressionList = NewHandleClear(sizeof(OSType));
    HLock(compressionList);
    *(OSType *)*compressionList = kVP8CodecFormatType;

    err = SCSetInfo(stdVideo, scCompressionListType, &compressionList);

    if (err)
    {
        dbg_printf("[WebM] Couldn't set vp8 to only codec in list err = %d\n", err);
        goto bail;
    }

    OSType manufacturer = kGoogManufacturer;
    err = SCSetInfo(stdVideo, scCodecManufacturerType, &manufacturer);

    if (err)
    {
        dbg_printf("[WebM] Couldn't set scCodecManufacturerType err = %d\n", err);
        goto bail;
    }


    //Set the image to show in the test box
    TimeScale movieTime = GetMovieTime(globals->setdlg_movie, NULL);

    if (movieTime == 0)
        movieTime = GetMoviePosterTime(globals->setdlg_movie);

    err = GetMoviesError();
    dbg_printf("[Webm] Set picture to time %ld\n", movieTime);

    if (!err)
    {
        currentPictureHandle = GetMoviePict(globals->setdlg_movie, movieTime);

        if (currentPictureHandle != NULL)
        {
            Rect rect;
            QDGetPictureBounds(currentPictureHandle, &rect);
            short testFlags = scPreferScalingAndCropping | scDontDetermineSettingsFromTestImage;
            err = SCSetTestImagePictHandle(stdVideo, currentPictureHandle, &rect, testFlags);
        }
    }

    //this does the user dialog and waits until they exit
    err = SCRequestSequenceSettings(stdVideo);

    if (err) goto bail;

    err = SCGetSettingsAsAtomContainer(stdVideo, &globals->videoSettingsAtom);
    err = SCGetInfo(stdVideo, scCodecSettingsType, &globals->videoSettingsCustom);
bail:

    if (stdVideo != NULL)
        CloseComponent(stdVideo);

    if (currentPictureHandle != NULL)
        DisposeHandle((Handle)currentPictureHandle);

    if (compressionList != NULL)
        DisposeHandle(compressionList);

    return err;
}

ComponentResult getVideoComponentInstace(WebMExportGlobalsPtr glob, ComponentInstance *videoCI)
{
    ComponentResult err =noErr;
    OpenADefaultComponent(StandardCompressionType, StandardCompressionSubType, videoCI);
    if (err) goto bail;
    
    if (glob->videoSettingsAtom == NULL)
    {
        getDefaultVP8Atom(glob);
    }

    err = SCSetSettingsFromAtomContainer(*videoCI, glob->videoSettingsAtom);
    if (err) goto bail;
    if (glob->videoSettingsCustom != NULL)
    {
        err = SCSetInfo(*videoCI, scCodecSettingsType, &glob->videoSettingsCustom);            
    }
bail:
    return err;
}
