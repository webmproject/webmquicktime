// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

//
// Implements two reader subclasses: MkvReaderQT and MkvBufferedReaderQT
// that can be passed to the mkvparser object for its use in reading WebM data.
// See www.webmproject.org for more info.
//

#include "mkvreaderqt.hpp"

extern "C" {
#include "log.h"
}


static void ReadCompletion(Ptr request, long refcon, OSErr readErr);


//-----------------------------------------------------------------------------
MkvReaderQT::MkvReaderQT()
    : m_length(0), m_dataHandler(NULL), data_is_stream_(false) {
}


//-----------------------------------------------------------------------------
MkvReaderQT::~MkvReaderQT() {
  Close();
}


//-----------------------------------------------------------------------------
// MkvReaderQT::Open
// Find and open an appropriate data handler for the given data
// reference and query its capabilities and optionally file size.
//
int MkvReaderQT::Open(Handle dataRef, OSType dataRefType, bool query_size) {
  if (dataRef == NULL)
    return -11;

  if (m_dataHandler)
    return -2;

  long fileSize = 0;

  // Retrieve the best data handler component to use with the given
  // data reference, for read purpoases.  Then open the returned
  // component using standard Component Manager calls.
  OSType err;
  ComponentInstance dataHandler = 0;
  err = OpenAComponent(GetDataHandler(dataRef, dataRefType, kDataHCanRead),
                       &dataHandler);
  if (err) return -3;

  // Provide a data reference to the data handler.
  // Then you may start reading and/or writing
  // movie data from that data reference.
  err = DataHSetDataRef(dataHandler, dataRef);
  if (err) return -4;

  // Open a read path to the current data reference.
  // You need to do this before your component can read data using a
  // data handler component.
  err = DataHOpenForRead(dataHandler);
  if (err) return -5;

  data_is_stream_ = (dataRefType == URLDataHandlerSubType);
  UInt32 flags;
  err = DataHGetInfoFlags(dataHandler, &flags);
  if (!err && (flags & kDataHInfoFlagNeverStreams))
    data_is_stream_ = false;
  err = noErr;

  m_length = -1;

  if (query_size) {
    // Get the size, in bytes, of the current data reference.
    // This is functionally equivalent to the File Manager's GetEOF function.
    dbg_printf("[WebM Import] DataHGetFileSize ()...\n");
    err = DataHGetFileSize(dataHandler, &fileSize);
    if (!err)
      m_length = fileSize;

    dbg_printf("[WebM Import] file size = %ld (err: %ld)\n", fileSize, err);
  }

  m_dataHandler = dataHandler;
  return 0;
}


//-----------------------------------------------------------------------------
// MkvReaderQT::Close
// Release the data handler component.
//
void MkvReaderQT::Close() {
  if (m_dataHandler) {
    CloseComponent(m_dataHandler);
    m_dataHandler = NULL;
  }
}


//-----------------------------------------------------------------------------
int MkvReaderQT::Read(long long position, long length, unsigned char* buffer) {
  // sanity checks
  if ((m_dataHandler == NULL) || (position < 0) || (length < 0) ||
      (m_length >= 0 && position >= m_length))
    return -1;

  if (length == 0)
    return 0;

  if (length != 1)
    dbg_printf("MkvReaderQT::Read() len = %ld\n", length);

  // Schedule a synchronous read operation (no refcon, schdule record
  // nor completion callback specified).
  OSType err;
  err = DataHScheduleData(m_dataHandler, (Ptr) buffer, position, length,
                          0, NULL, NULL);
  if (err)
    return -2;

  return 0;
}


//-----------------------------------------------------------------------------
int MkvReaderQT::Length(long long* total, long long* available) {
  if (total)
    *total = m_length;

  if (available)
    *available = m_length;

  return 0;
}


//-----------------------------------------------------------------------------
// MkvReaderQT::IsStreaming
// Is our data actually a network stream?
//
bool MkvReaderQT::IsStreaming() const {
  return data_is_stream_;
}


//-----------------------------------------------------------------------------
MkvBufferedReaderQT::MkvBufferedReaderQT()
    : bufDataSize(0), bufStartFilePos(0), bufCurFilePos(0), bufEndFilePos(0),
      m_PendingReadSize(0), chunk_size_(kReadChunkSize), eos_(false) {
  bufDataMax = sizeof(buf);
}


//-----------------------------------------------------------------------------
MkvBufferedReaderQT::~MkvBufferedReaderQT() {
}


//-----------------------------------------------------------------------------
int MkvBufferedReaderQT::Open(Handle dataRef, OSType dataRefType,
                              bool query_size) {
  int status = MkvReaderQT::Open(dataRef, dataRefType, query_size);
  if (!status)
    read_completion_cb = NewDataHCompletionUPP(ReadCompletion);

  return status;
}


//-----------------------------------------------------------------------------
void MkvBufferedReaderQT::Close() {
  MkvReaderQT::Close();
  if (read_completion_cb) {
    DisposeDataHCompletionUPP(read_completion_cb);
    read_completion_cb = NULL;
  }
}


//-----------------------------------------------------------------------------
int MkvBufferedReaderQT::Read(long long requestedPos, long requestedLen,
                              unsigned char* outbuf) {
  // NOTE: any dbg_printf() in this function has big impact on runtime
  // performance.
  if ((requestedPos < bufStartFilePos) ||
      (requestedPos >= bufEndFilePos) ||
      ((requestedPos + requestedLen) > bufEndFilePos)) {
    // Practically, shouldn't happen - we're accurately reporting the
    // amount of available data and it never decreases...
    return mkvparser::E_BUFFER_NOT_FULL;
  }

  // read from buffer
  if (requestedLen == 1) {
    outbuf[0] = buf[requestedPos - bufStartFilePos];
  } else if (requestedLen > 1) {
    memcpy(outbuf, buf + requestedPos - bufStartFilePos, requestedLen);
  }

  bufCurFilePos = requestedPos + requestedLen;
  return 0;
}


//-----------------------------------------------------------------------------
int MkvBufferedReaderQT::Length(long long* total, long long* available) {
  if (total)
    *total = m_length;

  if (available)
    *available = bufEndFilePos;

  return 0;
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::RequestFillBuffer
// Request an asynchronous read, of the given size, from the file into
// the buffer.
//
int MkvBufferedReaderQT::RequestFillBuffer(long request_size) {
  if (m_PendingReadSize != 0) {
    // The previous request is still pending - ignore the current one.
    return 0;
  }

  dbg_printf("RequestFillBuffer...\n");
  dbg_printf("MkvBufferedReaderQT::buf %ld [%ld - %ld - %ld] %ld\n",
             bufDataSize, bufStartFilePos, bufCurFilePos, bufEndFilePos,
             bufDataMax);

  if (m_length >= 0 && (bufEndFilePos + request_size > m_length)) {
    request_size = m_length - bufEndFilePos;
  }

  // if requested size wouldn't fit into available space in buffer,
  // and currently consumed more than 3/4 of data in buffer, then
  // compact the buffer.
  if ((bufDataSize + request_size > bufDataMax) &&
      ((bufCurFilePos - bufStartFilePos) >
       (bufEndFilePos - bufStartFilePos) * 0.75)) {
    CompactBuffer(request_size);
  }

  if (bufDataSize + request_size > bufDataMax) {
    // Still not enough available buffer space.
    // Here we could resize the buffer if it was dynamically
    // allocated. But since it's not - just indicate the error condition.
    return kFillBufferNotEnoughSpace;
  }

  wide ofst;
  ofst.hi = 0;
  ofst.lo = bufEndFilePos;
  m_PendingReadSize = request_size;
  readErr = DataHReadAsync(m_dataHandler, buf + bufDataSize, request_size,
                           &ofst, read_completion_cb, (long) this);
  dbg_printf("MkvBufferedReaderQT::RequestFillBuffer() scheduled %ld bytes"
             " @ %ld, error=%d\n", request_size, bufEndFilePos, readErr);
  if (readErr)
    m_PendingReadSize = 0;
  return readErr;
}


//-----------------------------------------------------------------------------
// Same as above, just with a previously set request size.
int MkvBufferedReaderQT::RequestFillBuffer() {
  return RequestFillBuffer(chunk_size_);
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::CompactBuffer
// Free some space in the buffer by removing part of the already
// consumed data.
//
void MkvBufferedReaderQT::CompactBuffer(long requestedSize) {
  long remainingDataSize = (bufDataMax - bufDataSize);
  long consumedDataSize = (bufCurFilePos - bufStartFilePos);
  long killDataSize =
      (consumedDataSize > kReadChunkSize) ?
      (consumedDataSize - kReadChunkSize) : (consumedDataSize * 0.50);
  if (requestedSize > remainingDataSize) {
    if (killDataSize > (requestedSize - remainingDataSize)) {
      dbg_printf("CompactBuffer() consumedDataSize=%ld, killDataSize=%ld\n",
                 consumedDataSize, killDataSize);
      memmove(&buf[0], &buf[killDataSize], (bufDataSize - killDataSize));
      bufDataSize -= killDataSize;
      bufStartFilePos += killDataSize;
    }
    else {
      dbg_printf("CompactBuffer() FAIL. Wait for more data to be consumed.\n");
    }
  }
  else
    dbg_printf("CompactBuffer() NOP, don't need to compact at this time.\n");
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::TaskDataHandler
// Give the data handler some CPU time to hande the asynchronous requests.
//
void MkvBufferedReaderQT::TaskDataHandler() {
  DataHTask(m_dataHandler);
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::TaskDataHandler
// Set the default read chunk size.
//
long MkvBufferedReaderQT::SetChunkSize(long size) {
  chunk_size_ = size < kMaxReadChunkSize ? size : kMaxReadChunkSize;
  return chunk_size_;
}


long MkvBufferedReaderQT::GetChunkSize() const {
  return chunk_size_;
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::RequestPending
// Is an asynchronous fill buffer request already pending?
//
bool MkvBufferedReaderQT::RequestPending() const {
  return (m_PendingReadSize != 0);
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::EOS
// Has the end of data been reached?
//
bool MkvBufferedReaderQT::EOS() const {
  return eos_ || bufEndFilePos == m_length;
}


//-----------------------------------------------------------------------------
// MkvBufferedReaderQT::ReadCompleted
// Handle one completed asynchronous read request.
//
void MkvBufferedReaderQT::ReadCompleted(Ptr request, OSErr read_error) {
  if (read_error) {
    if (read_error == eofErr)
      eos_ = true;
    else
      readErr = read_error;
  } else {
    bufEndFilePos += m_PendingReadSize; // incr file position by amount
                                        // that was just read into buf
    bufDataSize += m_PendingReadSize;   // incr size of data in buf
  }

  m_PendingReadSize = 0;
  dbg_printf("...ReadCompletion (filePos=%ld) [ptr=%p] = %d\n", bufEndFilePos,
             request, read_error);
}


//-----------------------------------------------------------------------------
// ReadCompletion
// C -> C++ wrapper to be able to pass callback to QuickTime data
// handler on asynchronous read requests.
//
static void ReadCompletion(Ptr request, long refcon, OSErr read_error) {
  MkvBufferedReaderQT* reader = (MkvBufferedReaderQT*) refcon;
  reader->ReadCompleted(request, read_error);
}
