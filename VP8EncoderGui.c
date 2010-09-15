
// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#if __APPLE_CC__
#include <QuickTime/QuickTime.h>
#else
#include <ConditionalMacros.h>
#include <Endian.h>
#include <ImageCodec.h>
#endif

#include <stdio.h>

#include "log.h"
#include "Raw_debug.h"

#include "VP8CodecVersion.h"
#define HAVE_CONFIG_H "vpx_codecs_config.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vpx_codec_impl_top.h"
#include "vpx/vpx_codec_impl_bottom.h"
#include "vpx/vp8cx.h"

#include "VP8Encoder.h"
#include "VP8EncoderGui.h"

//TODO change this!!
#define kWebMExportBundleID "com.google.google-qt.vp8codec"

static void setUIntFromControl(unsigned int * i, WindowRef w, int id)
{
    ControlID   cID = {'VP8A', id};
    ControlRef  ref;
    unsigned int originalVal = *i; //used for debugging
    
    OSStatus gotControl = GetControlByID(w, &cID, &ref);
    if(gotControl !=0)
    {
        dbg_printf("[VP8E] error couldn't get control %d\n", id);
        return;
    }
    ControlKind kind;
    OSStatus gotKind= GetControlKind(ref,&kind);
    
    if (kind.kind == 'eutx')
    {
        CFStringRef string;    
        Size size;
        GetControlData(ref,kControlEditTextPart, kControlEditTextCFStringTag,
                   sizeof( CFStringRef ),&string, &size);
        if (size != 0 && CFStringGetLength(string) !=0)
        {
            SInt32 gcdInt = CFStringGetIntValue(string);
            *i = gcdInt;        
        }
        else 
        {
            *i=UINT_MAX;
        }

        dbg_printf("[VP8E] SetUIntfromControl from %d to %d size %d - %d\n",
                   originalVal, *i, size, CFStringGetLength(string));
    }
    else if (kind.kind == 'cbbx' || kind.kind == 'cbox')
    {
        *i = GetControl32BitValue(ref);
        dbg_printf("[VP8E] SetUIntfromControl from %d to %d\n",
                   originalVal, *i);
    }
    else
    {
        dbg_printf("[VP8E] Error unhandled control type %d\n", kind.kind);
    }
    
    
}

static ComponentResult settingsFromGui(VP8customSettings c, WindowRef w)
{
    int i;
    for (i=0 ;i<28; i++)
    {
        setUIntFromControl(&c[i], w, i);
    }
    
    return noErr;
}
static void setControlFromUInt(unsigned int i, WindowRef w, int id)
{
    ControlID   cID = {'VP8A', id};
    ControlRef  ref;
    
    OSStatus gotControl = GetControlByID(w, &cID, &ref);
    if(gotControl !=0)
    {
        dbg_printf("[VP8E] error couldn't get control %d\n", id);
        return;
    }
    ControlKind kind;
    OSStatus gotKind= GetControlKind(ref,&kind);
    
    if (kind.kind == 'eutx')
    {
        if (i != UINT_MAX)
        {
            char buf[100];
            sprintf(buf, "%d",i);
            CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, buf,kCFStringEncodingASCII);
            SetControlData(ref,kControlEditTextPart, kControlEditTextCFStringTag,
                       sizeof( CFStringRef ),&string);
            dbg_printf("[VP8E] Set Control %d, to %s size %d\n",id, buf);
        }
    }
    else if (kind.kind == 'cbbx' || kind.kind == 'cbox')
    {
        if (i == UINT_MAX) i=0;
        SetControl32BitValue(ref,i );
        dbg_printf("[VP8E] Set Control %d, to %d size %d\n",id, i);
    }
    else
    {
        dbg_printf("[VP8E] Error unhandled control type %d\n", kind.kind);
    }
    
    
}
static ComponentResult settingsToGui(VP8customSettings c, WindowRef w)
{
    int i;
    for (i=0 ;i<28; i++)
    {
        setControlFromUInt(c[i], w, i);
    }
    
    return noErr;
}


static ComponentResult GetWindowAdvanced(WindowRef *window)
{
    ComponentResult err = noErr;
    CFBundleRef bundle = NULL;
    IBNibRef    nibRef = NULL;
    
    bundle = CFBundleGetBundleWithIdentifier(CFSTR(kWebMExportBundleID));
    
    if (bundle == NULL)
    {
        dbg_printf("[VP8e]  Error :: CFBundleGetBundleWithIdentifier\n");
        err = readErr;
        goto bail;
    }
    
    err = CreateNibReferenceWithCFBundle(bundle, CFSTR("WebMExport"), &nibRef);
    if (err)
    {
        dbg_printf("[VP8e]  >> Error :: CreateNibReferenceWithCFBundle\n");
        goto bail;
    }
    
    err = CreateWindowFromNib(nibRef, CFSTR("Advanced"), window);
    
    if (err)
    {
        dbg_printf("[VP8e]  >> Error :: CreateWindowFromNib\n");
        goto bail;
    }
    
bail:
    
    if (nibRef)
        DisposeNibReference(nibRef);
    
    
    return err;
}

pascal OSStatus AdvancedWindowEventHandler(EventHandlerCallRef inHandler, EventRef inEvent, void *inUserData)
{
    WindowRef window = NULL;
    HICommand command;
    OSStatus rval = eventNotHandledErr;
    VP8EncoderGlobals globals = (VP8EncoderGlobals) inUserData;
    
    dbg_printf("[vp8e] :: SettingsWindowEventHandler()\n", (UInt32) globals);
    
    window = ActiveNonFloatingWindow();
    
    if (window == NULL)
        goto bail;
    
    GetEventParameter(inEvent, kEventParamDirectObject, typeHICommand, NULL, sizeof(HICommand), NULL, &command);
    
    dbg_printf("[WebM]   | [%08lx] :: SettingsWindowEventHandler('%4.4s')\n", (UInt32) globals, (char *) &command.commandID);
    
    switch (command.commandID)
    {
        case kHICommandOK:
            settingsFromGui(globals->settings, window);
            QuitAppModalLoopForWindow(window);
            rval = noErr;
            break;
            
        case kHICommandCancel:
            QuitAppModalLoopForWindow(window);
            rval = noErr;
            break;
        default:
            break;
    }
    
bail:
    return rval;
}




ComponentResult runAdvancedWindow(VP8EncoderGlobals globals)
{
    Boolean     portChanged = false;
    WindowRef window = NULL;
    CGrafPtr    savedPort;

    EventTypeSpec eventList[] = {{kEventClassCommand, kEventCommandProcess}};
    EventHandlerUPP AdvancedWindowEventHandlerUPP = NewEventHandlerUPP(AdvancedWindowEventHandler);
    
    dbg_printf("[VP8e %lld] runAdvancedWindow()\n", (UInt32) globals);
    GetWindowAdvanced(&window);
    
    portChanged = QDSwapPort(GetWindowPort(window), &savedPort);
    dbg_printf("[VP8e %lld] runAdvancedWindow() portChanged %d \n", (UInt32) globals, portChanged);
    
    
    InstallWindowEventHandler(window, AdvancedWindowEventHandlerUPP, GetEventTypeCount(eventList), eventList, globals, NULL);
    
    settingsToGui(globals->settings, window);
    
    ShowWindow(window);
    
    RunAppModalLoopForWindow(window);
bail:
    if (window)
    {
        if (portChanged)
        {
            portChanged = QDSwapPort(savedPort, NULL);
            dbg_printf("[VP8e %lld] runAdvancedWindow() revert: portChanged %d \n", (UInt32) globals, portChanged);
        }
        
        DisposeWindow(window);
    }
    
    if (AdvancedWindowEventHandlerUPP)
        DisposeEventHandlerUPP(AdvancedWindowEventHandlerUPP);
    
    
}