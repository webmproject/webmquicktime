// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.




#define OUTPUT_FILE "/var/tmp/webm_debug.txt"
#include <QuickTime/QuickTime.h>
#include "log.h"
static void _dbg_printf(FILE *logFile, const char *s);


#if ENABLE_DEBUG_LOG
#include <sys/time.h>
#include <stdarg.h>
void log_time(FILE *logFile, const char *id, const char *fmt, ...)
{
    if (logFile == 0)
        return;

    va_list ap;
    struct timeval now;
    char info[80];

    va_start(ap, fmt);
    vsnprintf(info, 80, fmt, ap);
    info[79] = 0;
    va_end(ap);

    gettimeofday(&now, NULL);
    fprintf(logFile, "%s, %d%06d, -- %s\n", id, now.tv_sec, now.tv_usec, info);
}
#else
void log_time(FILE *logFile, const char *id, const char *fmt, ...) {}
#endif  // ENABLE_DEBUG_LOG

void dbg_printf(const char *s, ...)
{
#ifdef ENABLE_DEBUG_LOG
    char buffer[1024];
    va_list args;
    va_start(args, s);
    vsprintf(buffer, s, args);
    va_end(args);

    _dbg_printf(NULL, buffer);
#endif
}

static void _dbg_printf(FILE *logFile, const char *s)
{
#if ENABLE_DEBUG_LOG
    bool bCloseLogFile = logFile == NULL;

    if (logFile)
    {
        char c[512];
        time_t time_val = time(NULL);
        struct tm *now = NULL;
        now = localtime(&time_val);
        sprintf(c, "/var/tmp/vp8_debug_%d-%d:%d:%d.txt", getpid(), now->tm_hour, now->tm_min, now->tm_sec);
        logFile = fopen(c, "w+");
    }
    else
    {
        logFile = fopen(OUTPUT_FILE, "a");
        time_t time_val = time(NULL);
        struct tm *now = NULL;
        now = localtime(&time_val);
        fprintf(logFile,    "%d-%d:%d:%d  --  ", getpid(), now->tm_hour, now->tm_min, now->tm_sec);
    }

    fprintf(logFile, s);

    if (bCloseLogFile)
        fclose(logFile);

#endif
}

void dbg_dumpBytes(unsigned char *bytes, int size)
{
#if ENABLE_DEBUG_LOG
    int i;
    FILE *logFile = fopen(OUTPUT_FILE, "a");
    time_t time_val = time(NULL);
    struct tm *now = NULL;
    now = localtime(&time_val);
    fprintf(logFile,    "%d-%d:%d:%d\n", getpid(), now->tm_hour, now->tm_min, now->tm_sec);

    int mysize = size < 3000 ? size : 3000;

    for (i = 0; i < mysize; i++)
    {
        if (i % 40 == 0)
        {
            fprintf(logFile, "\n");
        }

        fprintf(logFile, "%x ", bytes[i]);
    }

    fprintf(logFile, "\n");
    fclose(logFile);
#endif
}


static void _dbg_dumpAtom(QTAtomContainer container, QTAtom head, int level)
{
#ifdef ENABLE_DEBUG_LOG
    short numChildren = QTCountChildrenOfType(container, head, 0);
    QTAtomType atomType;
    QTAtomID id;
    QTGetAtomTypeAndID(container, head, &atomType, &id);
    int i;
    char *indent = malloc(level + 1);
    memset(indent, ' ', level);
    indent[level] = 0;
    dbg_printf("%s id %d type %4.4s children %d\n", indent, id, (char *)&atomType, numChildren);
    free(indent);

    //print all children
    int ref;
    QTAtom curChild, nextChild;
    curChild = 0;

    for (ref = 0; ref < numChildren; ref++)
    {
        ComponentResult err = QTNextChildAnyType(container, head, curChild, &nextChild);

        if (err) break;

        _dbg_dumpAtom(container, nextChild, level + 1);
        curChild = nextChild;
    }

#endif
}

void dbg_dumpAtom(QTAtomContainer container)
{
#ifdef ENABLE_DEBUG_LOG
    _dbg_dumpAtom(container, 0, 0);
#endif
}
