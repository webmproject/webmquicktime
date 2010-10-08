// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#ifndef _WEBM_AUDIO_STREAM_H
#define _WEBM_AUDIO_STREAM_H


ComponentResult initVorbisComponent(WebMExportGlobalsPtr globals, GenericStreamPtr as);
ComponentResult compressAudio(GenericStreamPtr as);
ComponentResult write_vorbisPrivateData(GenericStreamPtr as, UInt8 **buf, UInt32 *bufSize);
ComponentResult getInputBasicDescription(GenericStreamPtr as, AudioStreamBasicDescription *inFormat);
ComponentResult initAudioStream(GenericStreamPtr as);

#endif
