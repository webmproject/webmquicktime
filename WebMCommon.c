// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#include "WebMCommon.h"

void initFrameQueue(WebMQueuedFrames *queue)
{
  queue->size =0;
  queue->maxSize = 0;
  queue->queue = NULL;
}

WebMBufferedFrame* getFrame(WebMQueuedFrames *queue)
{
  return queue->queue[0];
}

void releaseFrame(WebMQueuedFrames *queue)
{
  if (queue->size <=0)
    return;
  WebMBufferedFrame* frame = getFrame(queue);
  free(frame->data);
  free(frame);
  //advance all frames in the queue
  int i;
  for (i=1; i < queue->size; i ++)
    queue->queue[i-1] = queue->queue[i];
  queue->size -=1;
  
}

int addFrameToQueue(WebMQueuedFrames *queue, void * data, UInt32 dataSize, 
                    UInt64 timeMs, UInt16 frameType, UInt32 indx)
{
  if (queue->size +1 > queue->maxSize)
  {
    queue->maxSize += 1;
    queue->queue = realloc(queue->queue, (queue->maxSize) * sizeof(WebMBufferedFrame));
  }
  WebMBufferedFrame * frame = malloc(sizeof(WebMBufferedFrame));
  if (frame == NULL)
    return -1;
  frame->data = data;
  frame->size = dataSize;
  frame->timeMs = timeMs;
  frame->frameType = frameType;
  frame->indx = indx;
  
  queue->queue[queue->size] = frame;
  
  queue->size += 1;
  return 0;
}

int frameQueueSize(WebMQueuedFrames *queue)
{
  return queue->size;
}

int freeFrameQueue(WebMQueuedFrames *queue)
{
  while(queue->size > 0)
    releaseFrame(queue);
  free(queue->queue);
}

void initMovieGetParams(StreamSource *source)
{
  source->params.recordSize = sizeof(MovieExportGetDataParams);
  source->params.trackID = source->trackID;
  source->params.requestedTime = source->time;
  source->params.sourceTimeScale = source->timeScale;
  source->params.actualTime = 0;
  source->params.dataPtr = NULL;
  source->params.dataSize = 0;
  source->params.desc = NULL;
  source->params.descType = 0;
  source->params.descSeed = 0;
  source->params.requestedSampleCount = 1; // NOTE: 1 sample here for first audio request
  source->params.actualSampleCount = 0;
  source->params.durationPerSample = 1;
  source->params.sampleFlags = 0;
}

void dbg_printDataParams(StreamSource *get)
{
  MovieExportGetDataParams *p = &get->params;
  dbg_printf("[webM]  Data Params  %ld [%ld]"
             " %ld [%ld] [%ld @ %ld] %ld '%4.4s'\n",
             p->requestedSampleCount,
             p->actualSampleCount, p->requestedTime,
             p->actualTime, p->durationPerSample,
             p->sourceTimeScale, p->dataSize,
             (char *) &p->descType);
}

ComponentResult initStreamSource(StreamSource *source, TimeScale scale,
                                 long trackID, MovieExportGetPropertyUPP propertyProc,
                                 MovieExportGetDataUPP getDataProc, void *refCon)
{
  ComponentResult err = noErr;
  
  dbg_printf("[WebM] InitStreamSource %d timescale = %d\n", trackID, scale);
  
  source->trackID = trackID;
  source->propertyProc = propertyProc;
  source->dataProc = getDataProc;
  source->refCon = refCon;
  source->timeScale = scale;
  
  memset(&source->params, 0, sizeof(MovieExportGetDataParams));
  source->params.recordSize = sizeof(MovieExportGetDataParams);
  source->params.trackID = source->trackID;
  source->params.sourceTimeScale = scale;
  source->eos = false;
  source->time = 0;
  
  return err;
}

double getTimeAsSeconds(StreamSource *source)
{
  double val = (source->time * 1.0) / (source->timeScale * 1.0);
  return val;
}
