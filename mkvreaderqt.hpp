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

//
// MkvReaderQT
// Subclass of IMkvReader that knows about QuickTime dataRef and dataHandler objects, rather than plain file io.
//
class MkvReaderQT : public mkvparser::IMkvReader
{
  MkvReaderQT(const MkvReaderQT&);
  MkvReaderQT& operator=(const MkvReaderQT&);
public:
  MkvReaderQT();
  virtual ~MkvReaderQT();

  int Open(Handle dataRef, OSType dataRefType);
  void Close();
  bool IsOpen() const;

  virtual int Read(long long position, long length, unsigned char* buffer);
  virtual int Length(long long* total, long long* available);

  DataHandler m_dataHandler;  // ComponentInstance

private:
  long long m_length;
  Handle m_dataRef; // might not need to store this after open()

};


//
// MkvBufferedReaderQT
// Subclass of MkvReaderQT that uses async io to fill buffer.
//
class MkvBufferedReaderQT : public MkvReaderQT
{
  MkvBufferedReaderQT(const MkvBufferedReaderQT&);
  MkvBufferedReaderQT& operator=(const MkvBufferedReaderQT&);
public:
  MkvBufferedReaderQT();
  virtual ~MkvBufferedReaderQT();
  virtual int Read(long long position, long length, unsigned char* buffer);
  void InitBuffer();
  static void ReadAsync(MkvBufferedReaderQT* reader, long requestedLen = kReadChunkSize); //or kReadChunkSize
  void CompactBuffer(long requestedSize = 0);

  long m_PendingReadSize; // size requested by ReadAsync, nonzero if async read still outstanding.
  OSErr readErr;          // async read will set this
  unsigned char buf[kReadBufferMaxSize];
  long bufDataSize;       // size of data in buf
  long bufDataMax;
  long bufStartFilePos;   // long long
  long bufCurFilePos;
  long bufEndFilePos;     // file position already read info buf so far

private:
  //std::queue<unsigned char> m_buffer;
  //std::deque<unsigned char> m_buffer;
  //size_t m_chunksize;
  long long m_previousReadPos;

  DataHCompletionUPP  read_completion_cb;

};

#endif
