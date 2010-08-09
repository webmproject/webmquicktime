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
#include "WebMExportGuiAudio.h"

ComponentResult OpenVorbisDlg(WebMExportGlobalsPtr globals, WindowRef window)
{
    ComponentResult err = noErr;

    ComponentInstance stdAudio;
    SCExtendedProcs xProcs;

    dbg_printf("[WebM]  >> [%08lx] :: OpenVorbisDlg() \n", (UInt32) globals);

    if (globals->audioSettingsAtom == NULL)
        getDefaultVorbisAtom(globals);

    err = OpenADefaultComponent(StandardCompressionType, StandardCompressionSubTypeAudio, &stdAudio);

    if (err) return err;

    err = SCSetSettingsFromAtomContainer(stdAudio, globals->audioSettingsAtom);

    //Make vorbis the only available setting
    OSType formatList[] = {kAudioFormatXiphVorbis};
    err = QTSetComponentProperty(stdAudio, kQTPropertyClass_SCAudio,
                                 kQTSCAudioPropertyID_ClientRestrictedCompressionFormatList,
                                 sizeof(formatList),
                                 formatList);

    if (err) goto bail;

    //this does the user dialog and waits until they exit
    err = SCRequestImageSettings(stdAudio);

    if (err) goto bail;

    err = SCGetSettingsAsAtomContainer(stdAudio, &globals->audioSettingsAtom);
bail:

    if (stdAudio != NULL)
        CloseComponent(stdAudio);

    if (err)
        dbg_printf("[webm] open vorbis dialog err = %d", err);

    return err;
}


ComponentResult getDefaultVorbisAtom(WebMExportGlobalsPtr globals)
{
    Boolean tmpbool;
    ComponentResult err = noErr;
    ComponentInstance inst = NULL;  //if the user passes in null component
    err = OpenADefaultComponent(StandardCompressionType, StandardCompressionSubTypeAudio, &inst);

    err = QTGetComponentProperty(inst, kQTPropertyClass_SCAudio,
                                 kQTSCAudioPropertyID_SampleRateIsRecommended,
                                 sizeof(Boolean),
                                 &tmpbool, NULL);

    if (!err && tmpbool)
        globals->audioBSD.mSampleRate = 0.0;

    //make sure vorbis is the default setting
    err = QTSetComponentProperty(inst, kQTPropertyClass_SCAudio,
                                 kQTSCAudioPropertyID_BasicDescription,
                                 sizeof(AudioStreamBasicDescription), &globals->audioBSD);

    if (err) goto bail;

    err = SCGetSettingsAsAtomContainer(inst, &globals->audioSettingsAtom);
bail:

    if (inst != NULL)
        CloseComponent(inst);

    dbg_printf("[WebM] QTSetComponentProperty  to default %d \n", err);

    return err;
}

