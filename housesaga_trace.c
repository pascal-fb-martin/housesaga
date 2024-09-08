/* housesaga - A log consolidation and storage service.
 *
 * Copyright 2019, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housesaga_trace.c - The live trace log consolidation module of HouseSaga.
 *
 * This module is responsible for storing local and clients traces to disk.
 *
 * Trace reports use a JSON format similar to the one used for events.
 *
 * This module also implement a clone of the houselog.c C API, so
 * that HouseSaga can records its own traces without going into
 * a vicious circle..
 *
 * SYNOPSYS:
 *
 * void housesaga_trace_initialize (int argc, const char **argv);
 *
 *    Initialize the environment required to consolidate trace logs. This
 *    must be the first function that the application calls.
 *
 * -- houselog.c API clone --
 *
 * void houselog_trace (const char *file, int line, const char *level,
 *                       const char *object,
 *                       const char *format, ...);
 *
 *    Record a new trace. The first 3 parameters are handled by macros:
 *    HOUSE_INFO, HOUSE_WARNING, HOUSE_FAILURE.
 *
 * -- end of houselog.c clone --
 */

#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "echttp.h"
// #include "echttp_static.h"
#include "echttp_json.h"
#include "houselog.h"

#include "housesaga.h"
#include "housesaga_trace.h"
#include "housesaga_storage.h"


/* Record a new trace.
 * Such a trace might have been received from a client service,
 * or may be of a local  origin (see function houselog_trace() below).
 */
static void housesaga_trace_new (const struct timeval *timestamp,
                                 const char *host,
                                 const char *app,
                                 const char *file,
                                 int line,
                                 const char *level,
                                 const char *object,
                                 const char *text) {

    static char TraceHeader[] =
        "TIMESTAMP,HOST,APP,LEVEL,FILE,LINE,OBJECT,DESCRIPTION";

    char buffer[1024];

    snprintf (buffer, sizeof(buffer), "%ld.%03d,%s,%s,%s,%d,%s,%s,\"%s\"",
              timestamp->tv_sec, timestamp->tv_usec / 1000,
              host, app, file, line, level, object, text);

    housesaga_storage_save ("trace", timestamp->tv_sec, TraceHeader, buffer);
}

/* Local clone for the houselog.c API.
 * For now, traces in HouseSaga are only for debug purpose.
 * Overall traces have not been used much, compared to events.
 */
void houselog_trace (const char *file, int line, const char *level,
                     const char *object,
                     const char *format, ...) {

    va_list ap;
    char text[1024];
    struct timeval timestamp;

    gettimeofday (&timestamp, 0);

    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    housesaga_trace_new (&timestamp, housesaga_host(), "saga",
                         file, line, level, object, text);
    housesaga_storage_flush ();
}

/* Decode a report of traces from a source client.
 * The JSON format is the same as the format reported to web clients
 * by these sources. Sharing the same format reduces the amount of
 * code on the source side.
 */
static const char *housesaga_getjsonstring (ParserToken *parsed,
                                            const char *path) {
    int item = echttp_json_search(parsed, path);
    if (item < 0) return 0;
    if (parsed[item].type != PARSER_STRING) return 0;
    return parsed[item].value.string;
}

static struct timeval housesaga_getjsontime (ParserToken *parsed,
                                             const char *path) {
    struct timeval result = {0, 0};
    int item = echttp_json_search(parsed, path);
    if (item < 0) return result;
    if (parsed[item].type != PARSER_INTEGER) return result;
    result.tv_sec = parsed[item].value.integer / 1000;
    result.tv_usec = (parsed[item].value.integer % 1000) * 1000;
    return result;
}

static int housesaga_getjsonint (ParserToken *parsed, const char *path) {
    int item = echttp_json_search(parsed, path);
    if (item < 0) return 0;
    if (parsed[item].type != PARSER_INTEGER) return 0;
    return (int)(parsed[item].value.integer);;
}

static const char *housesaga_webtraces (const char *method, const char *uri,
                                        const char *data, int length) {

    static ParserToken *TraceParsed = 0;
    static int   TraceTokenAllocated = 0;
    static char *TraceBuffer = 0;

    if (strcmp (method, "POST")) return ""; // Only POST is supported.

    if (TraceBuffer) free (TraceBuffer);
    TraceBuffer = strdup (data);

    int count = echttp_json_estimate(data);
    if (count > TraceTokenAllocated) {
        if (TraceParsed) free (TraceParsed);
        TraceTokenAllocated = count;
        TraceParsed = calloc (count, sizeof(ParserToken));
    }
    const char *error = echttp_json_parse (TraceBuffer, TraceParsed, &count);

    // TBD: decode JSON, register traces.
    const char *host = housesaga_getjsonstring (TraceParsed, ".host");
    const char *app = housesaga_getjsonstring (TraceParsed, ".apps[0]");
    if (!host || !app) return "";

    char path[128];
    snprintf (path, sizeof(path), ".%s.traces", app);
    int traces = echttp_json_search(TraceParsed, path);
    if (TraceParsed[traces].type != PARSER_ARRAY) return "";

    int i;
    for (i = 0; i < TraceParsed[traces].length; ++i) {
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        int trace = echttp_json_search(TraceParsed+traces, path);
        if (trace < 0) break;
        trace += traces;
        struct timeval timestamp = housesaga_getjsontime (TraceParsed+trace, "[0]");
        const char *file = housesaga_getjsonstring (TraceParsed+trace, "[1]");
        int line = housesaga_getjsonint (TraceParsed+trace, "[2]");
        const char *level = housesaga_getjsonstring (TraceParsed+trace, "[3]");
        const char *object = housesaga_getjsonstring (TraceParsed+trace, "[4]");
        const char *text = housesaga_getjsonstring (TraceParsed+trace, "[5]");
        if (timestamp.tv_sec && file && line && level && object && text) {
            housesaga_trace_new (&timestamp, host, app,
                                 file, line, level, object, text);
        }
    }
    housesaga_storage_flush ();

    return "";
}

void housesaga_trace_initialize (int argc, const char **argv) {

    echttp_route_uri ("/saga/log/traces", housesaga_webtraces);

    // Alternate path for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/traces", housesaga_webtraces);
}

