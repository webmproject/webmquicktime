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


#ifndef MKVREADERQT_HPP_
#define MKVREADERQT_HPP_

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>
#include <queue>

#include "mkvparser.hpp"


//
// MkvReaderQT
// Subclass of IMkvReader that knows about QuickTime dataRef and
// dataHandler objects, rather than plain file io.
//
class MkvReaderQT : public mkvparser::IMkvReader {
 public:
  // Patching the incomplete mkvparser::IMkvReader interface by making
  // up our custom error codes, yay!
  // These are used in Read() and Length() methods.
  enum {
    // Request completed successfully.
    kSuccess = 0,
    // Data handler not initialized or read request outside posible range.
    kInvalidArgsOrState = -1,
    // Some error occured while asking the data handler component to
    // read some data.
    kReadFailed = -2,
  };

  MkvReaderQT();
  virtual ~MkvReaderQT();

  // Finds and opens an appropriate data handler for the given data
  // reference and queries its capabilities and optionally file
  // size. Returns noErr on success, Mac/QuickTime error status otherwise.
  virtual int Open(Handle dataRef, OSType dataRefType, bool query_size = true);
  // Closes and releases the data handler component.
  virtual void Close();

  // IMkvReader interface - reads the requested amount of data,
  // storing it in the passed |buffer|. Returns 0 on success.
  virtual int Read(long long position, long length, unsigned char* buffer);
  // IMkvReader interface - reports the total size of the file and the
  // amount of data immediately available for reading. If the file
  // size is unknown sets |total| to -1. Returns 0 on success.
  virtual int Length(long long* total, long long* available);

  // Returns true if the data is coming from a network stream.
  bool is_streaming() const;

 protected:
  long long m_length;
  DataHandler m_dataHandler;  // ComponentInstance
  // Is the data coming from network, as opposed to local disk?
  bool is_streaming_;

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
  // Status codes for RequestFillBuffer() method.
  enum {
    // The buffer can't fit more unread data.
    kFillBufferNotEnoughSpace = 1,
  };

  static const long kReadBufferMaxSize = 4 * 1024 * 1024;
  static const long kReadChunkSize = 32 * 1024;
  static const long kMaxReadChunkSize = 1 * 1024 * 1024;

  MkvBufferedReaderQT();
  virtual ~MkvBufferedReaderQT();

  // Finds and opens an appropriate data handler for the given data
  // reference and queries its capabilities and optionally file
  // size. Returns noErr on success, Mac/QuickTime error status otherwise.
  virtual int Open(Handle dataRef, OSType dataRefType, bool query_size = true);
  // Closes and releases the data handler component.
  virtual void Close();

  // IMkvReader interface - reads the requested amount of data,
  // storing it in the passed |buffer|. Returns 0 on success.
  virtual int Read(long long position, long length, unsigned char* buffer);
  // IMkvReader interface - reports the total size of the file and the
  // amount of data immediately available for reading. If the file
  // size is unknown sets |total| to -1. Returns 0 on success.
  virtual int Length(long long* total, long long* available);

  // Requests an asynchronous read, of the given size, from the file
  // into the internal buffer.
  int RequestFillBuffer(long request_size);
  // Requests an asynchronous from the file into the internal buffer.
  // Will use default read size or one previously configured with
  // set_chunk_size().
  int RequestFillBuffer();
  // Frees some already consumed data from the internal buffer.
  void CompactBuffer(long requestedSize = 0);
  // Gives the data handler some CPU time to handle the asynchronous requests.
  void TaskDataHandler();
  // Sets the read chunk size used by RequestFillBuffer().
  long set_chunk_size(long chunk_size);
  // Returns the current read chunk size.
  long chunk_size() const;
  // Returns true if a buffer fill request is pending.
  bool RequestPending() const;
  // Returns true if the end of data been reached?
  bool eos() const;
  // Callback handler for completed asynchronous read requests.
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

#endif  // MKVREADERQT_HPP_
