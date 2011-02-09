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



//--------------------------------------------------------------------------------
MkvReaderQT::MkvReaderQT() :
  m_length(0), m_dataRef(NULL), m_dataHandler(NULL)
{
}

//--------------------------------------------------------------------------------
MkvReaderQT::~MkvReaderQT()
{
  Close();
}

//--------------------------------------------------------------------------------
int MkvReaderQT::Open(Handle dataRef, OSType dataRefType)
{
  if (dataRef == NULL)
    return -11;

  if ((m_dataRef) || (m_dataHandler))
    return -2;

  m_dataRef = dataRef;

  long fileSize = 0;

  // Retrieve the best data handler component to use with the given data reference, for read purpoases.
	// Then open the returned component using standard Component Manager calls.
  OSType err;
  ComponentInstance dataHandler = 0;
	err = OpenAComponent(GetDataHandler(dataRef, dataRefType, kDataHCanRead), &dataHandler);
	if (err) return -3;

  // Provide a data reference to the data handler.
	// Then you may start reading and/or writing
	// movie data from that data reference.
	err = DataHSetDataRef(dataHandler, dataRef);
	if (err) return -4;

  // Open a read path to the current data reference.
  // You need to do this before your component can read data using a data handler component.
	err = DataHOpenForRead(dataHandler);
	if (err) return -5;

  // Get the size, in bytes, of the current data reference.
  // This is functionally equivalent to the File Manager's GetEOF function.
	err = DataHGetFileSize(dataHandler, &fileSize);
	if (err) return -6;
  m_length = fileSize;

#if 0
  Boolean buffersReads;
  Boolean buffersWrites;
  DataHDoesBuffer(dataHandler,  &buffersReads, &buffersWrites);
  dbg_printf("DataHDoesBuffer() buffersReads = %d\n", buffersReads);
  long blockSize = 0;
  DataHGetPreferredBlockSize(dataHandler, &blockSize);
  dbg_printf("DataHGetPreferredBlockSize() is %ld\n", blockSize);
#endif

  // dbg_printf("[WebM Import] DataHGetFileSize = %d\n", fileSize);
  //  dbg_printf("[WebM Import] sizeof Header %d\n", sizeof(header));

  m_dataHandler = dataHandler;
  return 0;
}

//--------------------------------------------------------------------------------
void MkvReaderQT::Close()
{
  // ****
}

//--------------------------------------------------------------------------------
int MkvReaderQT::Read(long long position, long length, unsigned char* buffer)
{
  // sanity checks
  if ((m_dataRef == NULL) || (position < 0) || (length < 0) || (position >= m_length))
    return -1;

  if (length == 0)
    return 0;

  if (length != 1)
    dbg_printf("MkvReaderQT::Read() len = %ld\n", length);

  // DatHGetPreferredBlockSize()
  // MovieImportSetIdleManager(store, store->idleManager)

  // seek and read
	// This function provides both a synchronous and an asynchronous read interface. Synchronous read operations
	// work like the DataHGetData function--the data handler component returns control to the client program only
	// after it has serviced the read request. Asynchronous read operations allow client programs to schedule read
	// requests in the context of a specified QuickTime time base. The DataHandler queues the request and immediately
	// returns control to the caller. After the component actually reads the data, it calls the completion function -
	// calling of the completion will occurs in DataHTask. Not all data handlers support scheduling, if they don't
	// they'll complete synchronously and then call the completion function. Additionally as a note, some DataHandlers
	// support 64-bit file offsets, for example DataHScheduleData64, we're not using this call here but you could use
	// 64-bit versions first, and if they fail fall back to the older calls.
	OSType err;
  err = DataHScheduleData(m_dataHandler,	// DataHandler Component Instance
                          (Ptr)buffer,	// Specifies the location in memory that is to receive the data
                          position,	        	// Offset in the data reference from which you want to read
                          length,       // The number of bytes to read
                          0,            // refCon
                          NULL,         // pointer to a schedule record - NULL for Synchronous operation
                          NULL);        // pointer to a data-handler completion function - NULL for Syncronous operation
	if (err)
    return -2;

  // if we read less than what caller asked for, then return error.
  //if (size < size_t(length))
  //  return -1;

  return 0;
}

//--------------------------------------------------------------------------------
int MkvReaderQT::Length(long long* total, long long* available)
{
  if (total)
    *total = m_length;

  if (available)
    *available = m_length;

  return 0;
}



//--------------------------------------------------------------------------------
//
MkvBufferedReaderQT::MkvBufferedReaderQT() :
  bufDataSize(0), bufStartFilePos(0), bufCurFilePos(0), bufEndFilePos(0), m_PendingReadSize(0)
{
  bufDataMax = sizeof(buf);
  m_previousReadPos = 0;
}

//--------------------------------------------------------------------------------
MkvBufferedReaderQT::~MkvBufferedReaderQT()
{
  // ****
}

//--------------------------------------------------------------------------------
//
int MkvBufferedReaderQT::Read(long long requestedPos, long requestedLen, unsigned char* outbuf)
{
  // **** NOTE - any dbg_print() in this function has big impact on runtime performance.
  //dbg_printf("MkvBufferedReaderQT::Read() - requestedPos = %lld, requestedLen = %ld\n", requestedPos, requestedLen);
  //dbg_printf("\tbufStartFilePos = %lld, m_buffer.size = %lu\n", bufStartFilePos, m_buffer.size());
  //  dbg_printf("ReadPos\t%c\t%ld\tdelta=%ld\n", (requestedPos > m_previousReadPos)?'+':'-', requestedPos, (requestedPos-m_previousReadPos));

  m_previousReadPos = requestedPos;

  if ((requestedPos < bufStartFilePos) ||
      (requestedPos > bufEndFilePos) ||
      ((requestedPos + requestedLen - 1) > bufEndFilePos)) {  // m_bufHeadPos+m_buffer.size())) {
    // non-contiguous read, miss
    dbg_printf("\tNON-CONTIGUOUS READ, CACHE MISS requestedPos = %lld, requestedLen = %ld [%ld - %ld - %ld]\n", requestedPos, requestedLen, bufStartFilePos, bufCurFilePos, bufEndFilePos);
    int err = this->MkvReaderQT::Read(requestedPos, requestedLen, outbuf);
    return err;
  }


  //dbg_printf("\tm_bufpos=%lld, requestedLen=%ld, m_buffer.size() = %lu\n", m_bufpos, requestedLen, m_buffer.size());
#if 0
  // contiguous read (or empty buffer)
  // is the request larger than what we already have in buffer?
  if ((requestedPos + requestedLen - 1) > bufEndFilePos) {  // (m_bufHeadPos + m_buffer.size() - 1)) {
    dbg_printf("\tNOT ENOUGH IN BUFFER, read from file.\n");
    // not enough in buffer...
    // read from data handler (file)
    size_t growSize = (requestedLen < m_chunksize) ? m_chunksize : requestedLen; // grow buffer by chunksize or requestedLen, whichever is greater.
    unsigned char* tempBuf = (unsigned char*)malloc(growSize);
    int err = this->MkvReaderQT::Read(requestedPos, growSize, tempBuf);
    if (err != 0) {
      free(tempBuf);
      return err;
    }

    // append to existing buffer
    for (long i=0; i < growSize; i++) {
      m_buffer.push_back(tempBuf[i]);
    }

    free(tempBuf);
  }
#endif
  //dbg_printf("\tm_buffer.front = %1x\n", m_buffer.front());

  // read from buffer
  if (requestedLen > 0) {
    for (long i=0; i < requestedLen; i++) {
      outbuf[i] = buf[requestedPos - bufStartFilePos + i];
      //outbuf[i] = m_buffer[requestedPos - bufStartFilePos + i]; //.front();
      //m_buffer.pop();
      //m_bufpos++;
    }
    // ****  dbg_printf("BUF CACHE HIT!\n");
    bufCurFilePos = (requestedPos + requestedLen);
  }

  //dbg_printf("\toutbuf[0]=%1x\n", outbuf[0]);
  //dbg_printf("MkvBufferedReaderQT::Read() return.\n");
  //dbg_printf("\n");
  return 0;
}

//--------------------------------------------------------------------------------
void MkvBufferedReaderQT::ReadAsync(MkvBufferedReaderQT* reader, long requestedLen)
{
  if (reader->m_PendingReadSize != 0) {
    // if an async read is already pending, nop and return now.
    return;
  }

  dbg_printf("ReadAsync...\n");
  dbg_printf("MkvBufferedReaderQT::buf %ld [%ld - %ld - %ld] %ld\n", reader->bufDataSize, reader->bufStartFilePos, reader->bufCurFilePos, reader->bufEndFilePos, reader->bufDataMax);

  if (reader->read_completion_cb == NULL)
    reader->read_completion_cb = NewDataHCompletionUPP(ReadCompletion);

  // if requested size wouldn't fit into available space in buffer, and currently consumed more than 3/4 of data in buffer, then compact the buffer.
  if ((reader->bufDataSize + requestedLen > reader->bufDataMax) &&
      ((reader->bufCurFilePos - reader->bufStartFilePos) > (reader->bufEndFilePos - reader->bufStartFilePos) * 0.75)) {
    reader->CompactBuffer(requestedLen);
  }

  if (reader->bufDataSize + requestedLen <= reader->bufDataMax) {
    wide ofst;
    ofst.hi = 0;
    ofst.lo = reader->bufEndFilePos;
    reader->m_PendingReadSize = requestedLen;
    reader->readErr = DataHReadAsync(reader->m_dataHandler, reader->buf + reader->bufDataSize, requestedLen, &ofst, reader->read_completion_cb, (long)reader);
  }
  else {
    dbg_printf("MkvBufferedReaderQT::ReadAsync() FAIL - BUFFER FULL. bufDataSize=%ld, consumed=%ld\n", reader->bufDataSize, (reader->bufCurFilePos - reader->bufStartFilePos));
    reader->m_PendingReadSize = 0;
    DataHTask(reader->m_dataHandler);
    //ReadCompletion(NULL, (long)reader, noErr);  // **** maybe wait, and then call comletion, just to keep pump going...
  }
}


//--------------------------------------------------------------------------------
void MkvBufferedReaderQT::InitBuffer()
{
  if ((m_PendingReadSize == 0) && (bufDataSize == 0) && (bufEndFilePos == 0)) {
    dbg_printf("InitBuffer (sync) ...");
    long requestedLen = kReadChunkSize * 16;   // **** try larger initial chunk
    m_PendingReadSize = requestedLen;
    int err = this->MkvReaderQT::Read(0, requestedLen, buf);

    ReadCompletion(NULL, (long)this, err);

  }
  else {
    dbg_printf("InitBuffer FAIL.");
  }
}


//--------------------------------------------------------------------------------
static void ReadCompletion(Ptr request, long refcon, OSErr readErr)
{
  MkvBufferedReaderQT* reader = (MkvBufferedReaderQT*)refcon;
  reader->readErr = readErr;
  reader->bufEndFilePos += reader->m_PendingReadSize; // incr file position by amount that was just read into buf
  reader->bufDataSize += reader->m_PendingReadSize; // incr size of data in buf
  reader->m_PendingReadSize = 0;

  dbg_printf("...ReadCompletion (filePos=%ld)\n", reader->bufEndFilePos);


  long readSize = kReadChunkSize; // MkvBufferedReaderQT::kDefaultChunkSize;
  MkvBufferedReaderQT::ReadAsync(reader);   // , readSize);  use default arg
}


//--------------------------------------------------------------------------------
void MkvBufferedReaderQT::CompactBuffer(long requestedSize)
{
  long remainingDataSize = (bufDataMax - bufDataSize);
  long consumedDataSize = (bufCurFilePos - bufStartFilePos);
  long killDataSize = (consumedDataSize > kReadChunkSize) ? (consumedDataSize - kReadChunkSize) : (consumedDataSize * 0.50);   //* 0.5;   // 0.66;  // / 2;
  if (requestedSize > remainingDataSize) {
    if (killDataSize > (requestedSize - remainingDataSize)) {
      dbg_printf("CompactBuffer() consumedDataSize=%ld, killDataSize=%ld\n", consumedDataSize, killDataSize);
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


