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
#define VPX_CODEC_DISABLE_COMPAT 1
#define HAVE_CONFIG_H "vpx_codecs_config.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vpx_codec_impl_top.h"
#include "vpx/vpx_codec_impl_bottom.h"
#include "vpx/vp8dx.h"
#include "log.h"

#include "VP8CodecVersion.h"
#include "WebMExportVersions.h"
#include "Raw_debug.h"

// Data structures
typedef struct
{
    ComponentInstance           self;
    ComponentInstance           delegateComponent;
    ComponentInstance           target;
    long                        width;
    long                        height;
    vpx_codec_ctx_t             *ctx;
    /*  struct InternalPixelBuffer  storedFrameArray[kMaxStoredFrames];
        struct InternalPixelBuffer  immediateFrame;*/
    Handle                      wantedDestinationPixelTypes;
} VP8DecoderGlobalsRecord, *VP8DecoderGlobals;

typedef struct
{
    long        width;
    long        height;
    size_t      dataSize;
    int         storageIndex; // index in storedFrameArray of where this frame will go, if applicable
    Boolean     willBeStored; // if true, frame will go in storedFrameArray[storageIndex]; if false, immediateFrame.
    Boolean     decoded;
    char        pad[2];
} VP8DecoderRecord;

// Setup required for ComponentDispatchHelper.c
#define IMAGECODEC_BASENAME()       VP8_Decoder_
#define IMAGECODEC_GLOBALS()        VP8DecoderGlobals storage

#define CALLCOMPONENT_BASENAME()    IMAGECODEC_BASENAME()
#define CALLCOMPONENT_GLOBALS()     IMAGECODEC_GLOBALS()

#define QT_BASENAME()               CALLCOMPONENT_BASENAME()
#define QT_GLOBALS()                CALLCOMPONENT_GLOBALS()

#define COMPONENT_UPP_PREFIX()      uppImageCodec
#define COMPONENT_DISPATCH_FILE     "VP8DecoderDispatch.h"
#define COMPONENT_SELECT_PREFIX()   kImageCodec

#define GET_DELEGATE_COMPONENT()    (storage->delegateComponent)

#if __APPLE_CC__
#include <CoreServices/Components.k.h>
#include <QuickTime/ImageCodec.k.h>
#include <QuickTime/ImageCompression.k.h>
#include <QuickTime/ComponentDispatchHelper.c>
#else
#include <Components.k.h>
#include <ImageCodec.k.h>
#include <ImageCompression.k.h>
#include <ComponentDispatchHelper.c>
#endif

/* -- This Image Decompressor User the Base Image Decompressor Component --
    The base image decompressor is an Apple-supplied component
    that makes it easier for developers to create new decompressors.
    The base image decompressor does most of the housekeeping and
    interface functions required for a QuickTime decompressor component,
    including scheduling for asynchronous decompression.
*/

// Component Open Request - Required
pascal ComponentResult VP8_Decoder_Open(VP8DecoderGlobals glob, ComponentInstance self)
{
    dbg_printf("[vp8d - %08lx] VP8_Decoder_Open\n", (UInt32) glob);
    ComponentResult err;

    // Allocate memory for our globals, set them up and inform the component manager that we've done so
    glob = calloc(sizeof(VP8DecoderGlobalsRecord), 1);

    if (! glob)
    {
        err = memFullErr;
        goto bail;
    }

    SetComponentInstanceStorage(self, (Handle)glob);

    glob->self = self;
    glob->target = self;
    glob->ctx = NULL;

    // Open and target an instance of the base decompressor as we delegate
    // most of our calls to the base decompressor instance
    err = OpenADefaultComponent(decompressorComponentType, kBaseCodecType, &glob->delegateComponent);

    if (err)
        goto bail;

    err = ComponentSetTarget(glob->delegateComponent, self);

bail:
    dbg_printf("[vp8d - %08lx]  open %x = %d\n", (UInt32) glob, err);
    return err;
}

// Component Close Request - Required
pascal ComponentResult VP8_Decoder_Close(VP8DecoderGlobals glob, ComponentInstance self)
{
    dbg_printf("[vp8d - %08lx] VP8_Decoder_Close %x \n", (UInt32) glob);

    // Make sure to close the base component and dealocate our storage
    if (glob)
    {
        int frameIndex;

        dbg_printf("[vp8d - %08lx] VP8_Decoder_Close delegate \n", (UInt32)glob);

        if (glob->delegateComponent)
        {
            CloseComponent(glob->delegateComponent);
        }

        dbg_printf("[vp8d - %08lx] VP8_Decoder_Close closed delegate\n", (UInt32)glob);

        DisposeHandle(glob->wantedDestinationPixelTypes);
        glob->wantedDestinationPixelTypes = NULL;

        if (glob->ctx != NULL)
        {
            if (vpx_codec_destroy(glob->ctx))
                dbg_printf("[vp8d - %08lx] Failed to destroy codec\n", (UInt32) glob);

            free(glob->ctx);
        }

        free(glob);
    }

    dbg_printf("[vp8d - %08lx] close component exit\n", (UInt32) glob);

    return noErr;
}

// Component Version Request - Required
pascal ComponentResult VP8_Decoder_Version(VP8DecoderGlobals glob)
{
#pragma unused(glob)
    return kDecompressorVersion;
}

// Component Target Request
//      Allows another component to "target" you i.e., you call another component whenever
// you would call yourself (as a result of your component being used by another component)
pascal ComponentResult VP8_Decoder_Target(VP8DecoderGlobals glob, ComponentInstance target)
{
    glob->target = target;
    return noErr;
}

#pragma mark-

// ImageCodecInitialize
//      The first function call that your image decompressor component receives from the base image
// decompressor is always a call to ImageCodecInitialize. In response to this call, your image decompressor
// component returns an ImageSubCodecDecompressCapabilities structure that specifies its capabilities.
pascal ComponentResult VP8_Decoder_Initialize(VP8DecoderGlobals glob, ImageSubCodecDecompressCapabilities *cap)
{
#pragma unused(glob)
    dbg_printf("[vp8d - %08lx] VP8_Decoder_Initialize\n", (UInt32) glob);

    // Secifies the size of the ImageSubCodecDecompressRecord structure
    // and say we can support asyncronous decompression
    // With the help of the base image decompressor, any image decompressor
    // that uses only interrupt-safe calls for decompression operations can
    // support asynchronous decompression.
    cap->decompressRecordSize = sizeof(VP8DecoderRecord);
    cap->canAsync = true;

    // These fields were added in QuickTime 7.  Be safe.
    if (cap->recordSize > offsetof(ImageSubCodecDecompressCapabilities, baseCodecShouldCallDecodeBandForAllFrames))
    {
        // Tell the base codec that we are "multi-buffer aware".
        // This promises that we always draw using the ImageSubCodecDecompressRecord.baseAddr/rowBytes
        // passed to our ImageCodecDrawBand function, and that we always overwrite every pixel in the buffer.
        // It is important to set this in order to get optimal performance when playing through CoreVideo.
        cap->subCodecIsMultiBufferAware = true;

        // Tell the base codec that we support "out-of-order display times".
        // This is the same as saying that we support B frames, or frame reordering.
        // It is important to set this or the ICM will assume we do not support B frames,
        // and attempts to schedule frames in a display order that's different from their
        // decode order will fail.
        cap->subCodecSupportsOutOfOrderDisplayTimes = true;

        // Ask the base codec to call our ImageCodecDecodeBand function for every frame.
        // If you do not set this, then your ImageCodecDrawBand function must
        // manually ensure that the frame is decoded before drawing it.
        cap->baseCodecShouldCallDecodeBandForAllFrames = true;
    }

    return noErr;
}

// ImageCodecPreflight
//      The base image decompressor gets additional information about the capabilities of your image
// decompressor component by calling ImageCodecPreflight. The base image decompressor uses this
// information when responding to a call to the ImageCodecPredecompress function,
// which the ICM makes before decompressing an image. You are required only to provide values for
// the wantedDestinationPixelSize and wantedDestinationPixelTypes fields and can also modify other
// fields if necessary.
pascal ComponentResult VP8_Decoder_Preflight(VP8DecoderGlobals glob, CodecDecompressParams *decompressParams)
{
    dbg_printf("[vp8d - %08lx] VP8_Decoder_Preflight\n", (UInt32) glob);
    OSStatus err = noErr;
    CodecCapabilities *capabilities = decompressParams->capabilities;
    int widthRoundedUp, heightRoundedUp;
    int frameIndex;

    // Specify the minimum image band height supported by the component
    // bandInc specifies a common factor of supported image band heights -
    // if your component supports only image bands that are an even
    // multiple of some number of pixels high report this common factor in bandInc
    capabilities->bandMin = (**decompressParams->imageDescription).height;
    capabilities->bandInc = capabilities->bandMin;
    dbg_printf("[vp8d - %08lx] preflight bandMin %d bandInc %d\n", (UInt32) glob, capabilities->bandMin, capabilities->bandInc);

    // Indicate the pixel depth the component can use with the specified image
    capabilities->wantedPixelSize = 0; // set this to zero when using wantedDestinationPixelTypes

    if (NULL == glob->wantedDestinationPixelTypes)
    {
        glob->wantedDestinationPixelTypes = NewHandleClear(2 * sizeof(OSType));

        if (NULL == glob->wantedDestinationPixelTypes)
            return memFullErr;
    }

    decompressParams->wantedDestinationPixelTypes = (OSType **)glob->wantedDestinationPixelTypes;
    (*decompressParams->wantedDestinationPixelTypes)[0] = k422YpCbCr8PixelFormat; // also known as '2vuy'
    (*decompressParams->wantedDestinationPixelTypes)[1] = 0;

    // Specify the number of pixels the image must be extended in width and height if
    // the component cannot accommodate the image at its given width and height.
    // This codec must have output buffers that are rounded up to multiples of 16x16.
    glob->width = (**decompressParams->imageDescription).width;
    glob->height = (**decompressParams->imageDescription).height;
    dbg_printf("[vp8d - %08lx] Preflight Width %d Height %d\n", (UInt32) glob, glob->width, glob->height);

    widthRoundedUp = glob->width;
    heightRoundedUp = glob->height;

    if (0 != (widthRoundedUp & 15))
        widthRoundedUp = (widthRoundedUp + 15) & ~15;

    if (0 != (heightRoundedUp & 15))
        heightRoundedUp = (heightRoundedUp + 15) & ~15;

    capabilities->extendWidth = widthRoundedUp - glob->width;
    capabilities->extendHeight = heightRoundedUp - glob->height;

    dbg_printf("[vp8d - %08lx] Preflight Extend Width %d Extend Height %d\n", (UInt32) glob,
               capabilities->extendWidth, capabilities->extendHeight);


    // Init Codec
    if (glob->ctx == NULL)
        glob->ctx = calloc(1, sizeof(vpx_codec_ctx_t));

    if (vpx_codec_dec_init(glob->ctx, &vpx_codec_vp8_dx_algo, NULL, 0))
    {
        dbg_printf("[vp8d - %08lx] vpx_QT_Dx: Failed to initialize decoder: %s\n", (UInt32) glob, vpx_codec_error(glob->ctx));
        return paramErr;
    }


bail:
    return err;
}

// ImageCodecBeginBand
//      The ImageCodecBeginBand function allows your image decompressor component to save information about
// a band before decompressing it. This function is never called at interrupt time. The base image decompressor
// preserves any changes your component makes to any of the fields in the ImageSubCodecDecompressRecord
// or CodecDecompressParams structures. If your component supports asynchronous scheduled decompression, it
// may receive more than one ImageCodecBeginBand call before receiving an ImageCodecDrawBand call.
pascal ComponentResult VP8_Decoder_BeginBand(VP8DecoderGlobals glob, CodecDecompressParams *p, ImageSubCodecDecompressRecord *drp, long flags)
{
#pragma unused(glob)
    dbg_printf("[vp8d - %08lx] VP8_Decoder_BeginBand\n", (UInt32) glob);
    OSStatus err = noErr;
    vpx_codec_err_t vpx_err;
    vpx_codec_stream_info_t stream_info;
    VP8DecoderRecord *myDrp = (VP8DecoderRecord *)drp->userDecompressRecord;
    Boolean keyFrame, differenceFrame, droppableFrame;
    int storageIndex;

    myDrp->width = (**p->imageDescription).width;
    myDrp->height = (**p->imageDescription).height;
    dbg_printf("[vp8d - %08lx] VP8_Decoder_BeginBand resolution %dx%d\n", (UInt32) glob, myDrp->width, myDrp->height);

    // Unfortunately, the image decompressor API can not quite guarantee to tell the decompressor
    // how much data is available, because the deprecated API DecompressSequenceFrame does not take
    // a dataSize argument.  (That's why you should call DecompressSequenceFrameS instead.)
    // Here's the best effort we can make: if there's a data-loading proc, use the dataSize from the
    // image description; otherwise, use the bufferSize.
    if (drp->dataProcRecord.dataProc)
        myDrp->dataSize = (**p->imageDescription).dataSize;
    else
        myDrp->dataSize = p->bufferSize;

    dbg_printf("[vp8d - %08lx] VP8_Decoder_BeginBand: datasize %d\n", (UInt32) glob, myDrp->dataSize);

    // In some cases, a frame will be decoded and ready for display, but the display will be cancelled.
    // QuickTime's video media handler will remember that the frame has already been decoded,
    // and if appropriate, will schedule that frame for display without redecoding by using the
    // icmFrameAlreadyDecoded flag.
    // In that case, we should simply retrieve the frame from whichever buffer we put it in.
    myDrp->decoded = p->frameTime ? (0 != (p->frameTime->flags & icmFrameAlreadyDecoded)) : false;
    dbg_printf("[vp8d - %08lx] VP8_Decoder_BeginBand:  already decoded = %d\n", (UInt32) glob, myDrp->decoded);

    /*err = NaiveDecoder_DecodeFrameHeader( (const UInt8 *)drp->codecData, &keyFrame, &differenceFrame, &droppableFrame, &storageIndex );
    if( err )
        goto bail;*/
    stream_info.sz = sizeof(stream_info);
    stream_info.w = glob->width;
    stream_info.h = glob->height;
    //TODO based on previous comments the data size might not be garenteed
    vpx_err = vpx_codec_peek_stream_info(&vpx_codec_vp8_dx_algo, (unsigned char *)drp->codecData, myDrp->dataSize, &stream_info);
#if 0
    if (vpx_err)
    {
        int i;
        dbg_printf("[vp8d - %08lx] VP8_Decoder_BeginBand: Failed to get stream info: %s\n", (UInt32) glob, vpx_codec_err_to_string(vpx_err));
        //dbg_dumpBytes((unsigned char*)drp->codecData, myDrp->dataSize);
        //return paramErr;
        //dbg_printf("continueing anyhow !!! \n");
        return paramErr;
    }
#else
    // Ignore return value, but use is_kf field.  vpx_codec_peek_stream_info() returns error for non-keyframes. 
    dbg_printf("VP8_Decoder_BeginBand ignoring return value from vpx_codec_peek_stream_info.\n");
#endif
  
    keyFrame = stream_info.is_kf;
    storageIndex = 0;
    droppableFrame = 0;

    // Remember which internal buffer we're going to put this frame in when we decode it.
    myDrp->storageIndex = storageIndex;
    myDrp->willBeStored = ! droppableFrame;

    // Classify the frame so that the base codec can do the right thing.
    // It is very important to do this so that the base codec will know
    // which frames it can drop if we're in danger of falling behind.

    if (keyFrame)
    {
        // Key frames are resynchronization points in the sequence of frames.
        // No frames following a key frame can depend on information from frames before a key frame.
        // Note that the I frame at the start of an open GOP in MPEG-1/2 frame patterns is *not* a key frame.
        drp->frameType = kCodecFrameTypeKey;
    }
    else if (droppableFrame)
    {
        // Droppable frames are not stored; no later frames depend on information in them.
        // We decode them into the immediateFrame buffer, which is available for reuse
        // as soon as the frame is drawn.
        drp->frameType = kCodecFrameTypeDroppableDifference;
    }
    else
    {
        // Other frames are difference frames.
        drp->frameType = kCodecFrameTypeDifference;
    }

    dbg_printf("[vp8d - %08lx] VP8_Decoder_BeginBand: frame number %d key frame %d\n", (UInt32) glob, p->frameNumber, keyFrame);
bail:
    return err;
}

pascal ComponentResult VP8_Decoder_DecodeBand(VP8DecoderGlobals glob, ImageSubCodecDecompressRecord *drp, unsigned long flags)
{
    OSErr err = noErr;
    dbg_printf("[vp8d - %08lx]  VP8_Decoder_DecodeBand\n", (UInt32) glob);
    VP8DecoderRecord *myDrp = (VP8DecoderRecord *)drp->userDecompressRecord;
    ICMDataProcRecordPtr dataProc = drp->dataProcRecord.dataProc ? &drp->dataProcRecord : NULL;
    struct InternalPixelBuffer *destBuffer;

    {
        /* Poke the DataProc instance, if one exists. */
        ICMDataProcRecordPtr dataProc = drp->dataProcRecord.dataProc ? &drp->dataProcRecord : NULL;

        if (dataProc)
        {
            long bytesNeeded = myDrp->dataSize;
            dbg_printf("[vp8d - %08lx] VP8_Decoder_DecodeBand: poking dataProc for %ld bytes\n", (UInt32) glob, bytesNeeded);
            dataProc->dataProc((Ptr *)&drp->codecData, bytesNeeded, dataProc->dataRefCon);
        }
    }

    dbg_printf("[vp8d - %08lx]  call vpx_codec_decode with %d bytes of data\n", (UInt32) glob, myDrp->dataSize);
    //writeRaw("/var/tmp/vp8dump.vp8", myDrp->dataSize, (unsigned char *)drp->codecData);

    if (vpx_codec_decode(glob->ctx, (unsigned char *)drp->codecData, myDrp->dataSize, NULL, 0))
    {
        dbg_printf("[vp8d - %08lx] vpx_QT_Dx: Failed to decode frame: %s\n", (UInt32) glob, vpx_codec_error(glob->ctx));
        return paramErr;
    }

    dbg_printf("[vp8d - %08lx]  successfully decoded frame size %d\n", (UInt32) glob, myDrp->dataSize);

    myDrp->decoded = true;

bail:
    return err;
}

// ImageCodecDrawBand
//      The base image decompressor calls your image decompressor component's ImageCodecDrawBand function
// to decompress a band or frame. Your component must implement this function. If the ImageSubCodecDecompressRecord
// structure specifies a progress function or data-loading function, the base image decompressor will never call ImageCodecDrawBand
// at interrupt time. If the ImageSubCodecDecompressRecord structure specifies a progress function, the base image decompressor
// handles codecProgressOpen and codecProgressClose calls, and your image decompressor component must not implement these functions.
// If not, the base image decompressor may call the ImageCodecDrawBand function at interrupt time.
// When the base image decompressor calls your ImageCodecDrawBand function, your component must perform the decompression specified
// by the fields of the ImageSubCodecDecompressRecord structure. The structure includes any changes your component made to it
// when performing the ImageCodecBeginBand function. If your component supports asynchronous scheduled decompression,
// it may receive more than one ImageCodecBeginBand call before receiving an ImageCodecDrawBand call.
pascal ComponentResult VP8_Decoder_DrawBand(VP8DecoderGlobals glob, ImageSubCodecDecompressRecord *drp)
{
    OSErr err = noErr;
    VP8DecoderRecord *myDrp = (VP8DecoderRecord *)drp->userDecompressRecord;

    vpx_codec_iter_t iter = NULL;
    vpx_image_t *img;
    dbg_printf("[vp8d - %08lx] VP8_Decoder_DrawBand\n", (UInt32) glob);

    if (! myDrp->decoded)
    {
        // If you don't set the baseCodecShouldCallDecodeBandForAllFrames flag, or if you
        // need QuickTime 6 compatibility, you should double-check that the frame has been decoded here,
        // and decode if necessary:

        err = VP8_Decoder_DecodeBand(glob, drp, 0);

        if (err) goto bail;
    }

    img = vpx_codec_get_frame(glob->ctx, &iter);

    if (img)
    {
        dbg_printf("[vp8d - %08lx] vpx_QT_Dx_DrawBand: got image %dx%d!\n", (UInt32) glob, myDrp->width, myDrp->height);
        CopyPlanarYV12ToChunkyYUV422(myDrp->width, myDrp->height,
                                     img->planes[VPX_PLANE_Y], img->stride[VPX_PLANE_Y],
                                     img->planes[VPX_PLANE_U], img->stride[VPX_PLANE_U],
                                     img->planes[VPX_PLANE_V], img->stride[VPX_PLANE_V],
                                     drp->baseAddr, drp->rowBytes);

    }


    dbg_printf("[vp8d - %08lx] leaving vp8_decoder_drawband  %d\n", (UInt32) glob, err);

bail:
    return err;
}

// ImageCodecEndBand
//      The ImageCodecEndBand function notifies your image decompressor component that decompression of a band has finished or
// that it was terminated by the Image Compression Manager. Your image decompressor component is not required to implement
// the ImageCodecEndBand function. The base image decompressor may call the ImageCodecEndBand function at interrupt time.
// After your image decompressor component handles an ImageCodecEndBand call, it can perform any tasks that are required
// when decompression is finished, such as disposing of data structures that are no longer needed. Because this function
// can be called at interrupt time, your component cannot use this function to dispose of data structures; this
// must occur after handling the function. The value of the result parameter should be set to noErr if the band or frame was
// drawn successfully. If it is any other value, the band or frame was not drawn.
pascal ComponentResult VP8_Decoder_EndBand(VP8DecoderGlobals glob, ImageSubCodecDecompressRecord *drp, OSErr result, long flags)
{
#pragma unused(glob, drp,result, flags)

    return noErr;
}

// ImageCodecQueueStarting
//      If your component supports asynchronous scheduled decompression, the base image decompressor calls your image decompressor component's
// ImageCodecQueueStarting function before decompressing the frames in the queue. Your component is not required to implement this function.
// It can implement the function if it needs to perform any tasks at this time, such as locking data structures.
// The base image decompressor never calls the ImageCodecQueueStarting function at interrupt time.
pascal ComponentResult VP8_Decoder_QueueStarting(VP8DecoderGlobals glob)
{
#pragma unused(glob)

    return noErr;
}

// ImageCodecQueueStopping
//       If your image decompressor component supports asynchronous scheduled decompression, the ImageCodecQueueStopping function notifies
// your component that the frames in the queue have been decompressed. Your component is not required to implement this function.
// After your image decompressor component handles an ImageCodecQueueStopping call, it can perform any tasks that are required when decompression
// of the frames is finished, such as disposing of data structures that are no longer needed.
// The base image decompressor never calls the ImageCodecQueueStopping function at interrupt time.
pascal ComponentResult VP8_Decoder_QueueStopping(VP8DecoderGlobals glob)
{
#pragma unused(glob)

    return noErr;
}

// ImageCodecGetCompressedImageSize
//      Your component receives the ImageCodecGetCompressedImageSize request whenever an application calls the ICM's GetCompressedImageSize function.
// You can use the ImageCodecGetCompressedImageSize function when you are extracting a single image from a sequence; therefore, you don't have an
// image description structure and don't know the exact size of one frame. In this case, the Image Compression Manager calls the component to determine
// the size of the data. Your component should return a long integer indicating the number of bytes of data in the compressed image. You may want to store
// the image size somewhere in the image description structure, so that you can respond to this request quickly. Only decompressors receive this request.
pascal ComponentResult VP8_Decoder_GetCompressedImageSize(VP8DecoderGlobals glob, ImageDescriptionHandle desc, Ptr data, long dataSize, ICMDataProcRecordPtr dataProc, long *size)
{
#pragma unused(glob,desc,dataSize,dataProc)

    if (size == NULL)
        return paramErr;

    //••

    return unimpErr;
}

// ImageCodecGetCodecInfo
//      Your component receives the ImageCodecGetCodecInfo request whenever an application calls the Image Compression Manager's GetCodecInfo function.
// Your component should return a formatted compressor information structure defining its capabilities.
// Both compressors and decompressors may receive this request.
pascal ComponentResult VP8_Decoder_GetCodecInfo(VP8DecoderGlobals glob, CodecInfo *info)
{
    OSErr err = noErr;

    if (info == NULL)
    {
        err = paramErr;
    }
    else
    {
        CodecInfo **tempCodecInfo;

        err = GetComponentResource((Component)glob->self, codecInfoResourceType, 255, (Handle *)&tempCodecInfo);

        if (err == noErr)
        {
            *info = **tempCodecInfo;
            DisposeHandle((Handle)tempCodecInfo);
        }

        //TODO see if this line is needed
        /*else if(err == resNotFound) {
            memcpy(info->typeName, "\3VP8", 4);
            info->version=0;
            info->revisionLevel=0;
            info->vendor='vpx ';
            info->decompressFlags=0x10000420;
            info->compressFlags=0;
            info->formatFlags=codecInfoDepth24;
            info->compressionAccuracy = info->decompressionAccuracy = 128;
            info->compressionSpeed = info->decompressionSpeed = 200;
            info->compressionLevel=128;
            info->resvd=0;
            info->minimumHeight = info->minimumWidth= 1;
            info->decompressPipelineLatency = info->compressPipelineLatency = 0;
            info->privateData=0;
            err=noErr;
        }*/
    }

    return err;
}

#pragma mark-

// When building the *Application Version Only* make our component available for use by applications (or other clients).
// Once the Component Manager has registered a component, applications can find and open the component using standard
// Component Manager routines.
#if !STAND_ALONE && !TARGET_OS_WIN32
void RegisterVP8Decompressor(void);
void RegisterVP8Decompressor(void)
{
    ComponentDescription td;

    td.componentType = decompressorComponentType;
    td.componentSubType = FOUR_CHAR_CODE('VP80');
    td.componentManufacturer = kGoogManufacturer;
    td.componentFlags = cmpThreadSafe;
    td.componentFlagsMask = 0;

    RegisterComponent(&td, (ComponentRoutineUPP)VP8_Decoder_ComponentDispatch, 0, NULL, NULL, NULL);
}
#endif // !STAND_ALONE && TARGET_OS_WIN32
