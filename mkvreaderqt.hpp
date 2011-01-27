// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#ifndef MKVREADERQT_HPP
#define MKVREADERQT_HPP

#include "mkvparser.hpp"

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>
#include <queue>

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

private:
  long long m_length;
  Handle m_dataRef; // might not need to store this after open()
  DataHandler m_dataHandler;  // ComponentInstance

};


class MkvBufferedReaderQT : public MkvReaderQT
{
  MkvBufferedReaderQT(const MkvBufferedReaderQT&);
  MkvBufferedReaderQT& operator=(const MkvBufferedReaderQT&);
public:
  MkvBufferedReaderQT();
  virtual ~MkvBufferedReaderQT();
  virtual int Read(long long position, long length, unsigned char* buffer);
  static const size_t kDefaultChunkSize = 1024;

private:
  std::queue<unsigned char> m_buffer;
  long long m_bufpos;
  size_t m_chunksize;

};

#endif
