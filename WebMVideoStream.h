// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#ifndef _WEBM_VIDEO_STREAM_H_
#define _WEBM_VIDEO_STREAM_H_ 1
//helper functions
ComponentResult openDecompressionSession(GenericStreamPtr si);
ComponentResult openCompressionSession(WebMExportGlobalsPtr globals, GenericStreamPtr si);

ComponentResult compressNextFrame(WebMExportGlobalsPtr globals, GenericStreamPtr si);
ComponentResult initVideoStream(GenericStreamPtr vs);
ComponentResult startPass(GenericStreamPtr vs,int pass);
ComponentResult endPass(GenericStreamPtr vs);
#endif
