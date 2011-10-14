// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

//
// Defines two reader subclasses: MkvReaderQT and MkvBufferedReaderQT
// that can be passed to the mkvparser object for its use in reading WebM data.
// See www.webmproject.org for more info.
//


#ifndef MKVREADERQT_HPP
#define MKVREADERQT_HPP

#include "mkvparser.hpp"

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>
#include <queue>


#define kReadBufferMaxSize  (4*1024*1024)
#define kReadChunkSize        (32*1024)
static const long kMaxReadChunkSize = 1 * 1024 * 1024;

// Status codes for RequestFillBuffer() method.
enum {
  kFillBufferNotEnoughSpace = 1,
};


//
// MkvReaderQT
// Subclass of IMkvReader that knows about QuickTime dataRef and
// dataHandler objects, rather than plain file io.
//
class MkvReaderQT : public mkvparser::IMkvReader {
 public:
  MkvReaderQT();
  virtual ~MkvReaderQT();

  virtual int Open(Handle dataRef, OSType dataRefType, bool query_size = true);
  virtual void Close();

  virtual int Read(long long position, long length, unsigned char* buffer);
  virtual int Length(long long* total, long long* available);

  bool IsStreaming() const;

 protected:
  long long m_length;
  DataHandler m_dataHandler;  // ComponentInstance
  // Is the data coming from network, as opposed to local disk?
  bool data_is_stream_;

 private:
  MkvReaderQT(const MkvReaderQT&);
  MkvReaderQT& operator=(const MkvReaderQT&);
};


//
// MkvBufferedReaderQT
// Subclass of MkvReaderQT that uses async io to fill buffer.
//
class MkvBufferedReaderQT : public MkvReaderQT {
 public:
  MkvBufferedReaderQT();
  virtual ~MkvBufferedReaderQT();

  virtual int Open(Handle dataRef, OSType dataRefType, bool query_size = true);
  virtual void Close();

  virtual int Read(long long position, long length, unsigned char* buffer);
  virtual int Length(long long* total, long long* available);

  int RequestFillBuffer(long request_size);
  int RequestFillBuffer();
  void CompactBuffer(long requestedSize = 0);
  void TaskDataHandler();
  long SetChunkSize(long chunk_size);
  long GetChunkSize() const;
  bool RequestPending() const;
  bool EOS() const;
  void ReadCompleted(Ptr request, OSErr read_error);

 private:
  MkvBufferedReaderQT(const MkvBufferedReaderQT&);
  MkvBufferedReaderQT& operator=(const MkvBufferedReaderQT&);

  // Size requested by RequestFillBuffer, nonzero if async read still
  // outstanding.
  long m_PendingReadSize;
  OSErr readErr;          // async read will set this
  unsigned char buf[kReadBufferMaxSize];
  long bufDataSize;       // size of data in buf
  long bufDataMax;
  long bufStartFilePos;   // long long
  long bufCurFilePos;
  long bufEndFilePos;     // file position already read info buf so far
  // Number of bytes to read at once.
  long chunk_size_;
  // Has the end of data been reached?
  bool eos_;
  DataHCompletionUPP  read_completion_cb;
};

#endif
