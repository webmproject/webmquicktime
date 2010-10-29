// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#define thng_RezTemplateVersion 2

#include <Carbon/Carbon.r>
#include <QuickTime/QuickTime.r>

#include "VP8CodecVersion.h"
#include "WebMExportVersions.h"

//
// WebM Import component
//
// canMovieImportValidateFile
// canMovieImportAvoidBlocking
// canMovieImportWithIdle
// canMovieImportPartial | canMovieImportInPlace | hasMovieImportMIMEList | \
// canMovieImportValidateDataReferences | \

#define kImporterFlags canMovieImportFiles | canMovieImportInPlace | canMovieImportDataReferences | cmpThreadSafe | hasMovieImportMIMEList | canMovieImportWithIdle

resource 'STR ' (263) {
  "WebM" "0.0.1" " see http://webmproject.org"
};

resource 'thng' (263) {
  'eat ',							// Type			
	'WebM',							// SubType
	'vide',					// Manufacturer - for 'eat ' the media type supported by the component
	0,								// - use componentHasMultiplePlatforms
	0,
	0,
	0,
	'STR ',							// Name Type
	260,							// Name ID
	'STR ',							// Info Type
	263,							// Info ID
	0,								// Icon Type
	0,								// Icon ID
	kImporterFlags,
	componentHasMultiplePlatforms +			// Registration Flags 
	componentDoAutoVersion,
	0,										// Resource ID of Icon Family
  {
    kImporterFlags | cmpThreadSafe, 
    'dlle',
    263,
    platformIA32NativeEntryPoint,
  },
  0,0;
};

// Code Entry Point for Mach-O and Windows
resource 'dlle' (263) {
	"WebMImportComponentDispatch"
};

//thga
resource 'thga' (264) {
  'eat ',   // Type
  'WEBM',   // Subtype in ALL CAPS. It will match a suffix case insensitively.
  'vide',   // for 'eat ' the media type supported by the component
  kImporterFlags |
  movieImportSubTypeIsFileExtension,
  0,
  0,
  0,
  'STR ',
  263,
  0,
  0,
  0,
  0,
  'eat ',
  'WebM',
  'vide',
  0,
  0,
  'thnr', 263,
  cmpAliasOnlyThisFile
};

// thnr - public component resource
// same data that MovieImportGetMIMETypeList would return.
resource 'thnr' (263) {
  {
    'mime', 1, 0,
    'mime', 263, 0,
  
    'mcfg', 1, 0,
    'mcfg', 263, 0
  }
};

// mcfg - QT Media Configuration Resource
// If the kQTMediaConfigUsePluginByDefault flag is set, QuickTime will automatically register the MIME type for the QuickTime Plug-in with all browsers on both platforms.
resource 'mcfg' (263)
{
    kVersionDoesntMatter,
  {
    // determine which group this MIME type will be listed under in MIME configuration panel
    kQTMediaConfigVideoGroupID,
    
    kQTMediaConfigUsePluginByDefault
    | kQTMediaConfigCanUseApp
    | kQTMediaConfigCanUsePlugin
    | kQTMediaConfigBinaryFile,

    'WebM',       // MacOS file type when saved (OSType)
    0,
    
    // Component info used by QT plugin to find componet to open this type of file
    'eat ',
    'WebM',
    'vide',
    kImporterFlags,
    0,
    
    'WEBM',     // filename extension (ALL CAPS)
    kQTMediaInfoNetGroup,
    {
    },
    
    {
      "WebM Movie File",
      "webm",
      "QuickTime Player",
      "WebM Movie Importer",
      "Version 0.0.1, see http://webmproject.org"
    },
    
    // Array of MIME types that describe this media type
    {
      "video/webm"
    },
  }
};
    
// atom container resrouce to hold mime types. 
// Component's GetMIMETypeList simply loads this resource and returns it.
resource 'mime' (263) {
  {
    kMimeInfoMimeTypeTag,     1, "video/webm";
    kMimeInfoFileExtensionTag,  1, "webm";
    kMimeInfoDescriptionTag,    1, "WebM";
  };
};
    
