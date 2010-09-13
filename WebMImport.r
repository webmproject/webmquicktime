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

#define kImporterFlags canMovieImportFiles | canMovieImportDataReferences | cmpThreadSafe

resource 'STR ' (263) {
  "WebM" "0.0.1" " see http://webmproject.org"
};


resource 'thng' (263) {
  'eat ',							// Type			
	'WebM',							// SubType
	kGoogManufacturer,					// Manufacturer  
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

