// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


/*
 
 © Copyright 2005 Apple Computer, Inc. All rights reserved.
 
 IMPORTANT:  This Apple software is supplied to 
 you by Apple Computer, Inc. ("Apple") in 
 consideration of your agreement to the following 
 terms, and your use, installation, modification 
 or redistribution of this Apple software 
 constitutes acceptance of these terms.  If you do 
 not agree with these terms, please do not use, 
 install, modify or redistribute this Apple 
 software.
 
 In consideration of your agreement to abide by 
 the following terms, and subject to these terms, 
 Apple grants you a personal, non-exclusive 
 license, under Apple's copyrights in this 
 original Apple software (the "Apple Software"), 
 to use, reproduce, modify and redistribute the 
 Apple Software, with or without modifications, in 
 source and/or binary forms; provided that if you 
 redistribute the Apple Software in its entirety 
 and without modifications, you must retain this 
 notice and the following text and disclaimers in 
 all such redistributions of the Apple Software. 
 Neither the name, trademarks, service marks or 
 logos of Apple Computer, Inc. may be used to 
 endorse or promote products derived from the 
 Apple Software without specific prior written 
 permission from Apple.  Except as expressly 
 stated in this notice, no other rights or 
 licenses, express or implied, are granted by 
 Apple herein, including but not limited to any 
 patent rights that may be infringed by your 
 derivative works or by other works in which the 
 Apple Software may be incorporated.
 
 The Apple Software is provided by Apple on an "AS 
 IS" basis.  APPLE MAKES NO WARRANTIES, EXPRESS OR 
 IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED 
 WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY 
 AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING 
 THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE 
 OR IN COMBINATION WITH YOUR PRODUCTS.
 
 IN NO EVENT SHALL APPLE BE LIABLE FOR ANY 
 SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL 
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS 
 OF USE, DATA, OR PROFITS; OR BUSINESS 
 INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, 
 REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF 
 THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER 
 UNDER THEORY OF CONTRACT, TORT (INCLUDING 
 NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN 
 IF APPLE HAS BEEN ADVISED OF THE POSSIBILITY OF 
 SUCH DAMAGE.
 
 */




// Set to 1 == building Mac OS X
#define TARGET_REZ_CARBON_MACHO 1

#if TARGET_REZ_CARBON_MACHO

    #if defined(ppc_YES)
        // PPC architecture
#error ppc not supported
        #define TARGET_REZ_MAC_PPC 1
    #else
        #define TARGET_REZ_MAC_PPC 0
    #endif

    #if defined(i386_YES)
        // x86 architecture
        #define TARGET_REZ_MAC_X86 1
    #else
#error
		#define TARGET_REZ_MAC_X86 0
    #endif

    #define TARGET_REZ_WIN32 0
#else
#error Must be building on Windows
    #define TARGET_REZ_WIN32 1
#endif

#define DECO_BUILD 1
#define COMP_BUILD 1

#define thng_RezTemplateVersion 2

#if TARGET_REZ_CARBON_MACHO
    #include <Carbon/Carbon.r>
    #include <QuickTime/QuickTime.r>
#else
    #include "ConditionalMacros.r"
    #include "MacTypes.r"
    #include "Components.r"
    #include "ImageCodec.r"
#endif

#include "VP8CodecVersion.h"
#include "WebMExportVersions.h"

// These flags specify information about the capabilities of the component
// Works with 32-bit Pixel Maps
#define kCompressorFlags ( codecInfoDoes32 | codecInfoDoesTemporal | codecInfoDoesReorder | codecInfoDoesRateConstrain )
#define kDecompressorFlags ( codecInfoDoes32 | codecInfoDoesTemporal )

// These flags specify the possible format of compressed data produced by the component
// and the format of compressed files that the component can handle during decompression
// The component can decompress from files at 24-bit depths
#define kFormatFlags	( codecInfoDepth24 )

// Component Description
resource 'cdci' (255) {
	kCodecFormatName,				// Type
	1,								// Version
	1,								// Revision level
	kGoogManufacturer,					// Manufacturer
	kDecompressorFlags,				// Decompression Flags
	kCompressorFlags,				// Compression Flags
	kFormatFlags,					// Format Flags
	128,							// Compression Accuracy
	128,							// Decomression Accuracy
	200,							// Compression Speed
	200,							// Decompression Speed
	128,							// Compression Level
	0,								// Reserved
	1,								// Minimum Height
	1,								// Minimum Width
	0,								// Decompression Pipeline Latency
	0,								// Compression Pipeline Latency
	0								// Private Data
};

// Component Name
resource 'STR ' (256) {
	"VP8"
};

#if DECO_BUILD
resource 'thng' (256) {
	decompressorComponentType,		// Type			
	kVP8CodecFormatType,				// SubType
	kGoogManufacturer,					// Manufacturer
	0,								// - use componentHasMultiplePlatforms
	0,
	0,
	0,
	'STR ',							// Name Type
	256,							// Name ID
	'STR ',							// Info Type
	257,							// Info ID
	0,								// Icon Type
	0,								// Icon ID
	kDecompressorVersion,
	componentHasMultiplePlatforms +		// Registration Flags 
		componentDoAutoVersion,
	0,										// Resource ID of Icon Family
	{    
#if TARGET_REZ_CARBON_MACHO
    #if !(TARGET_REZ_MAC_PPC || TARGET_REZ_MAC_X86)
        #error "Platform architecture not defined, TARGET_REZ_MAC_PPC and/or TARGET_REZ_MAC_X86 must be defined!"
    #endif
    
    #if TARGET_REZ_MAC_PPC
        kDecompressorFlags | cmpThreadSafe, 
        'dlle',
        256,
        platformPowerPCNativeEntryPoint,
    #endif
    #if TARGET_REZ_MAC_X86
        kDecompressorFlags | cmpThreadSafe, 
        'dlle',
        256,
        platformIA32NativeEntryPoint,
    #endif
#endif

#if TARGET_OS_WIN32
#error "Windows build is not yet set up"
	kDecompressorFlags, 
	'dlle',
	256,
	platformWin32,
#endif
	},
	0, 0;
};

// Component Information
resource 'STR ' (257) {
	"VP8 Decompressor."
};

// Code Entry Point for Mach-O and Windows
resource 'dlle' (256) {
	"VP8_Decoder_ComponentDispatch"
};
#endif // DECO_BUILD

#if COMP_BUILD
resource 'thng' (258) {
	compressorComponentType,		// Type			
	kVP8CodecFormatType,				// SubType
	kGoogManufacturer,					// Manufacturer
	0,								// - use componentHasMultiplePlatforms
	0,
	0,
	0,
	'STR ',							// Name Type
	256,							// Name ID
	'STR ',							// Info Type
	258,							// Info ID
	0,								// Icon Type
	0,								// Icon ID
	kVP8CompressorVersion,
	componentHasMultiplePlatforms +			// Registration Flags 
		componentDoAutoVersion,
	0,										// Resource ID of Icon Family
	{
#if TARGET_REZ_CARBON_MACHO
    #if !(TARGET_REZ_MAC_PPC || TARGET_REZ_MAC_X86)
        #error "Platform architecture not defined, TARGET_REZ_MAC_PPC and/or TARGET_REZ_MAC_X86 must be defined!"
    #endif
    
    #if TARGET_REZ_MAC_PPC    
        kCompressorFlags | cmpThreadSafe, 
        'dlle',
        258,
        platformPowerPCNativeEntryPoint,
    #endif
    #if TARGET_REZ_MAC_X86
        kCompressorFlags | cmpThreadSafe, 
        'dlle',
        258,
        platformIA32NativeEntryPoint,
    #endif
#endif

#if TARGET_OS_WIN32
	kCompressorFlags, 
	'dlle',
	258,
	platformWin32,
#endif
	},
	0, 0;
};

// Component Information
resource 'STR ' (258) {
	"VP8 Encoder."
};

// Code Entry Point for Mach-O and Windows
resource 'dlle' (258) {
	"VP8_Encoder_ComponentDispatch"
};
#endif // COMP_BUILD


#define kExporterFlags canMovieExportFiles | canMovieExportValidateMovie | \
canMovieExportFromProcedures | movieExportMustGetSourceMediaType | \
hasMovieExportUserInterface | cmpThreadSafe


//name
resource 'STR ' (260) {
	"WebM"
};


resource 'thng' (262) {
	'spit',							// Type			
	'WebM',							// SubType
	kGoogManufacturer,					// Manufacturer  
	0,								// - use componentHasMultiplePlatforms
	0,
	0,
	0,
	'STR ',							// Name Type
	260,							// Name ID
	'STR ',							// Info Type
	262,							// Info ID
	0,								// Icon Type
	0,								// Icon ID
	kExporterFlags,
	componentHasMultiplePlatforms +			// Registration Flags 
	componentDoAutoVersion,
	0,										// Resource ID of Icon Family
	{
#if TARGET_REZ_CARBON_MACHO
#if !(TARGET_REZ_MAC_PPC || TARGET_REZ_MAC_X86)
#error "Platform architecture not defined, TARGET_REZ_MAC_PPC and/or TARGET_REZ_MAC_X86 must be defined!"
#endif
		
#if TARGET_REZ_MAC_PPC    
        kExporterFlags | cmpThreadSafe, 
        'dlle',
        262,
        platformPowerPCNativeEntryPoint,
#endif
#if TARGET_REZ_MAC_X86
        kExporterFlags | cmpThreadSafe, 
        'dlle',
        262,
        platformIA32NativeEntryPoint,
#endif
#endif
		
#if TARGET_OS_WIN32
		kExporterFlags, 
		'dlle',
		262,
		platformWin32,
#endif
	},
	0, 0;
};

// Component Information
resource 'STR ' (262) {
	"WebM"
};

// Code Entry Point for Mach-O and Windows
resource 'dlle' (262) {
	"WebMExportComponentDispatch"
};


