// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#ifndef _WEBM_AUDIO_STREAM_H
#define _WEBM_AUDIO_STREAM_H


ComponentResult initVorbisComponent(WebMExportGlobalsPtr globals, AudioStreamPtr as);
ComponentResult compressAudio(AudioStreamPtr as);
ComponentResult write_vorbisPrivateData(AudioStreamPtr as, UInt8 **buf, UInt32 *bufSize);
ComponentResult getInputBasicDescription(AudioStreamPtr as, AudioStreamBasicDescription *inFormat);
ComponentResult initAudioStream(AudioStreamPtr as);

#endif
