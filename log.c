// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.



#include "log.h"


#if ENABLE_DEBUG_LOG


#include <sys/time.h>
#include <stdarg.h>
#include <QuickTime/QuickTime.h>


#define OUTPUT_FILE "/var/tmp/webm_debug.txt"

static FILE *defaultLogFile = NULL;


static inline void _check_init_logfile()
{
    if (!defaultLogFile) {
        defaultLogFile = fopen(OUTPUT_FILE, "a");
    }
}


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
    fprintf(logFile, "%s, %d%06d, -- %s\n", id, (int) now.tv_sec,
            now.tv_usec, info);
}


void dbg_printf(const char *s, ...)
{
    va_list args;
    int count;
    time_t time_val = time(NULL);
    struct tm *now = NULL;

    _check_init_logfile();

    now = localtime(&time_val);
    fprintf(defaultLogFile, "%d-%02d:%02d:%02d  --  ", getpid(),
            now->tm_hour, now->tm_min, now->tm_sec);

    va_start(args, s);
    count = vfprintf(defaultLogFile, s, args);
    va_end(args);

    if (count < 0) {
        fprintf(defaultLogFile, "\ndbg_printf error (%d) in \"%s\" - aborting.\n", count, s);
        fflush(NULL);
        abort();
    }

    fflush(defaultLogFile);
}


void dbg_dumpBytes(unsigned char *bytes, int size)
{
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
}


static void _dbg_dumpAtom(QTAtomContainer container, QTAtom head, int level)
{
    short numChildren = QTCountChildrenOfType(container, head, 0);
    QTAtomType atomType;
    QTAtomID id;
    QTGetAtomTypeAndID(container, head, &atomType, &id);
    int i;
    char *indent = malloc(level + 1);
    memset(indent, ' ', level);
    indent[level] = 0;
    dbg_printf("%s id %ld type %4.4s children %d\n", indent, id, (char *)&atomType, numChildren);
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
}


void dbg_dumpAtom(QTAtomContainer container)
{
    _dbg_dumpAtom(container, 0, 0);
}


#else   //ENABLE_DEBUG_LOG


void log_time(FILE *logFile, const char *id, const char *fmt, ...) {};
void dbg_printf(const char *s, ...) {};
void dbg_dumpBytes(unsigned char *bytes, int size) {};
void dbg_dumpAtom(QTAtomContainer container) {};


#endif  //ENABLE_DEBUG_LOG
