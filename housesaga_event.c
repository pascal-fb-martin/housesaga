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
 * housesaga_event.c - The live log consolidation module of HouseSaga.
 *
 * This is responsible for keeping the latest logs in memory. This also
 * call the HouseSaga storage module when either of the conditions below
 * is met:
 *  - the buffer is full and the oldest item was not saved to storage.
 *  - there is at least one unsaved item and last save was N seconds ago.
 *
 * The origin of events are source clients, which report their new, unreported
 * events on their own.
 *
 * All event exchanges (source to web clients, sources to this service, and
 * this service to web clients) use the same JSON format, except that this
 * service report two more items for each event: host and application names.
 *
 * This way, the event page for HouseSaga is not very different from the
 * applications event page: only two more columns. Reusing the same web API
 * allows reusing the JavaScript module.
 *
 * In addition, using the exact same format to report from the source to the
 * web client and this service means that a single function can be used to
 * format both.
 *
 * This module also implement a clone of the houselog.c C API, so
 * that HouseSaga can records its own events without going into a
 * vicious circle..
 *
 * The event are stored in RAM in the order they were received. However this
 * does not necessarily match the chronological order of events, because of
 * source buffering and flush delays.
 *
 * TBD: create a secondary ordered list (ordered list of references), using
 * a top-down byte bucket sort algorithm, i.e. a tree of buckets, each with
 * 256 entries and collision lists, as that method is friendly to continuous
 * inserts. (most complex might be removal of "older" entries: double-linked
 * list will be required here.)
 *
 * SYNOPSYS:
 *
 * void housesaga_event_initialize (int argc, const char **argv);
 *
 *    Initialize the environment required to consolidate event logs. This
 *    must be the first function that the application calls.
 *
 * -- houselog.c API clone --
 *
 * void houselog_event (const char *category,
 *                      const char *object,
 *                      const char *action,
 *                      const char *format, ...);
 *
 *    Record a new event.
 *
 * -- end of houselog.c clone --
 *
 * void housesaga_event_background (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    processing, e.g. cleanup of expired resources, storage backup, etc.
 */

#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "echttp.h"
#include "echttp_json.h"
#include "echttp_sorted.h"
#include "houselog.h"

#include "housesaga.h"
#include "housesaga_event.h"
#include "housesaga_storage.h"

static const char  LogAppName[] = "saga";

struct EventRecord {
    struct timeval timestamp;
    long long id;
    int    unsaved;
    char   host[128];
    char   app[128];
    char   category[32];
    char   object[32];
    char   action[16];
    char   description[128];
};

#define HISTORY_DEPTH 256

static struct EventRecord EventHistory[HISTORY_DEPTH];
static int EventCursor = 0;
static long long EventLatestId = 0;

static echttp_sorted_list EventChronology;
static time_t EventLastSaved = 0;
static time_t EventSaveLimit = 0;


static void safecpy (char *d, const char *s, int size) {
    if (!d) return;
    if (!s) {
        d[0] = 0;
    } else {
        strncpy (d, s, size);
        d[size-1] = 0;
    }
}

static unsigned long long housesaga_timestamp2key (const struct timeval *t) {
    return t->tv_sec * 1000 + t->tv_usec / 1000;
}

static int housesaga_saveaction (void *data) {

    static char EventHeader[] =
        "TIMESTAMP,HOST,APP,CATEGORY,OBJECT,ACTION,DESCRIPTION";

    struct EventRecord *cursor = EventHistory + (intptr_t) data;

    if (cursor->unsaved) {
        char buffer[1024];

        if (cursor->timestamp.tv_sec > EventSaveLimit) return 0;

        snprintf (buffer, sizeof(buffer), "%lld.%03d,%s,%s,%s,%s,%s,\"%s\"",
                  (long long)(cursor->timestamp.tv_sec),
                  (int)(cursor->timestamp.tv_usec / 1000),
                  cursor->host,
                  cursor->app,
                  cursor->category,
                  cursor->object,
                  cursor->action,
                  cursor->description);
        housesaga_storage_save ("event", cursor->timestamp.tv_sec,
                                EventHeader, buffer);
        cursor->unsaved = 0;
    }
    return 1;
}

static void housesaga_event_save (int full) {

    time_t now = time(0);

    // In order to keep the historical log in chronological order, we delay
    // storing recent events as they could have not been all reported yet,
    // due to bufferization by the source. The delay here gives enough time
    // for all sources to have flushed their events out.
    // This delay does not apply when the event buffer is full: in that
    // case, we must save events at all cost.
    //
    EventSaveLimit = full ? now + 2 : now - 6;

    if (EventLastSaved) {
        echttp_sorted_ascending_from (EventChronology,
                                      EventLastSaved * 1000,
                                      housesaga_saveaction);
    } else {
        echttp_sorted_ascending (EventChronology, housesaga_saveaction);
    }
    housesaga_storage_flush();
    EventLastSaved = full ? now : EventSaveLimit;
}

/* Record a new event to the live buffer.
 * Such an event might have been received from a client service,
 * or may be of a local  origin (see function houselog_event() below).
 */
static void housesaga_event_new (const struct timeval *timestamp,
                                 const char *host,
                                 const char *app,
                                 const char *category,
                                 const char *object,
                                 const char *action,
                                 const char *text, int propagate) {

    struct EventRecord *cursor = EventHistory + EventCursor;

    if (!EventChronology) EventChronology = echttp_sorted_new();

    if (EventLatestId == 0) {
        // Seed the latest event ID based on the first event's time.
        // This makes it random enough to make its value change after
        // a restart.
        EventLatestId = (long long) (time(0) & 0xfffff);
    }
    EventLatestId += 1;

    cursor->timestamp = *timestamp;
    cursor->id = EventLatestId;
    safecpy (cursor->host, host, sizeof(cursor->host));
    safecpy (cursor->app, app, sizeof(cursor->app));
    safecpy (cursor->category, category, sizeof(cursor->category));
    safecpy (cursor->object, object, sizeof(cursor->object));
    safecpy (cursor->action, action, sizeof(cursor->action));
    safecpy (cursor->description, text, sizeof(cursor->description));
    cursor->unsaved = propagate;

    echttp_sorted_add (EventChronology,
                       housesaga_timestamp2key (&(cursor->timestamp)),
                       (void *)((long)EventCursor));

    if (timestamp->tv_sec < EventLastSaved) {
        // Hoops: we got a late event from a distant past. We need
        // to make sure it will be saved, even if out of order.
        EventLastSaved = timestamp->tv_sec;
    }

    EventCursor += 1;
    if (EventCursor >= HISTORY_DEPTH) EventCursor = 0;

    cursor = EventHistory + EventCursor;
    if (cursor->timestamp.tv_sec) {
        if (cursor->unsaved) housesaga_event_save(1); // Save before erased.

        echttp_sorted_remove (EventChronology,
                              housesaga_timestamp2key (&(cursor->timestamp)),
                              (void *)((long)EventCursor));
        cursor->timestamp.tv_sec = 0;
    }
}

/* Local clone for the houselog.c API.
 * This function plugs into the event reporting used by the HouseSaga web API.
 */
void houselog_event (const char *category,
                     const char *object,
                     const char *action,
                     const char *format, ...) {

    va_list ap;
    char text[128];
    struct timeval timestamp;

    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    gettimeofday (&timestamp, 0);

    housesaga_event_new (&timestamp, housesaga_host(), "saga", 
                         category, object, action, text, 1);
}

/* Local clone for the houselog.c API.
 * This function plugs into the event reporting used by the HouseSaga web API.
 */
void houselog_event_local (const char *category,
                           const char *object,
                           const char *action,
                           const char *format, ...) {

    va_list ap;
    char text[128];
    struct timeval timestamp;

    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    gettimeofday (&timestamp, 0);

    housesaga_event_new (&timestamp, housesaga_host(), "saga", 
                         category, object, action, text, 0);
}

static int housesaga_event_getheader (char *buffer, int size, const char *from) {

    char fromparam[256];

    echttp_content_type_json ();
    if (from) {
        snprintf (fromparam, sizeof(fromparam), ",\"from\":%s", from);
    } else {
        fromparam[0] = 0;
    }
    return snprintf (buffer, size,
                    "{\"host\":\"%s\",\"proxy\":\"%s\",\"apps\":[\"%s\"],"
                        "\"timestamp\":%lld,\"latest\":%lld,\"%s\":{\"invert\":true%s,\"latest\":%lld",
                    housesaga_host(), housesaga_portal(), LogAppName,
                    (long long)time(0), EventLatestId,
                    LogAppName, fromparam, EventLatestId);
    // (The second iteration of EventLatestId above is for compatibility only.)
}

static char WebFormatBuffer[128+HISTORY_DEPTH*(sizeof(struct EventRecord)+24)] = {0};
static int WebFormatLength = 0;
static const char *WebFormatPrefix = "";
static time_t WebFormatSinceSec = 0;
static int WebFormatSinceUSec = 0;

static int housesaga_webaction (void *data) {

    int size = sizeof(WebFormatBuffer) - 4; // Need room to complete the JSON.

    struct EventRecord *cursor = EventHistory + (intptr_t) data;

    if (!(cursor->timestamp.tv_sec)) return 1;

    // Stop when the time limit, if any, was reached.
    //
    if (cursor->timestamp.tv_sec <= WebFormatSinceSec) {
        if (cursor->timestamp.tv_sec < WebFormatSinceSec) return 0;
        if (cursor->timestamp.tv_usec < WebFormatSinceUSec) return 0;
    }

    int wrote = snprintf (WebFormatBuffer+WebFormatLength, size-WebFormatLength,
                          "%s[%lld%03d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%lld]",
                          WebFormatPrefix,
                          (long long)(cursor->timestamp.tv_sec),
                          (int)(cursor->timestamp.tv_usec/1000),
                          cursor->category,
                          cursor->object,
                          cursor->action,
                          cursor->description,
                          cursor->host,
                          cursor->app,
                          cursor->id);
    WebFormatPrefix = ",";

    if (WebFormatLength + wrote >= size) {
        WebFormatBuffer[WebFormatLength] = 0;
        return 0;
    }
    WebFormatLength += wrote;
    return 1;
}

// This request is deprecated. Use the "GET /log/events" request with
// the "known" parameter instead for a more efficient polling.
//
static const char *housesaga_weblatest (const char *method, const char *uri,
                                        const char *data, int length) {

    static char buffer[256];
    int written = housesaga_event_getheader (buffer, sizeof(buffer), 0);
    snprintf (buffer+written, sizeof(buffer)-written, "}}");
    return buffer;
}

static const char *housesaga_webget (void) {

    const char *known = echttp_parameter_get("known");
    if (known && (atoll (known) == EventLatestId)) {
        echttp_error (304, "Not Modified");
        return "";
    }

    const char *since = echttp_parameter_get("since");

    if (since) {
        long long sincevalue = atoll(since);
        WebFormatSinceSec = (time_t)(sincevalue / 1000);
        WebFormatSinceUSec = (int)((sincevalue % 1000) * 1000);
    } else {
        WebFormatSinceSec = 0;
        WebFormatSinceUSec = 0;
    }

    echttp_content_type_json ();

    WebFormatLength = housesaga_event_getheader (WebFormatBuffer,
                                                sizeof(WebFormatBuffer), 0);
    WebFormatLength += snprintf (WebFormatBuffer+WebFormatLength,
                                 sizeof(WebFormatBuffer)-WebFormatLength,
                                 ",\"events\":[");

    WebFormatPrefix = "";
    echttp_sorted_descending(EventChronology, housesaga_webaction);
    snprintf (WebFormatBuffer+WebFormatLength,
              sizeof(WebFormatBuffer)-WebFormatLength, "]}}");
    return WebFormatBuffer;
}

/* Decode a report of events from a source client.
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

static const char *housesaga_webpost (const char *data, int length) {

    static ParserToken *EventParsed = 0;
    static int   EventTokenAllocated = 0;
    static char *EventBuffer = 0;

    echttp_content_type_json ();

    if (EventBuffer) free (EventBuffer);
    EventBuffer = strdup (data);

    int count = echttp_json_estimate(data);
    if (count > EventTokenAllocated) {
        if (EventParsed) free (EventParsed);
        EventTokenAllocated = count;
        EventParsed = calloc (count, sizeof(ParserToken));
    }
    const char *error = echttp_json_parse (EventBuffer, EventParsed, &count);
    if (error) return ""; // Ignore bad data from applications.

    // TBD: decode JSON, register events.
    const char *host = housesaga_getjsonstring (EventParsed, ".host");
    const char *app = housesaga_getjsonstring (EventParsed, ".apps[0]");

    char path[128];
    snprintf (path, sizeof(path), ".%s.events", app);
    int events = echttp_json_search(EventParsed, path);
    if (EventParsed[events].type != PARSER_ARRAY) return "";

    int i;
    for (i = 0; i < EventParsed[events].length; ++i) {
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        int event = echttp_json_search(EventParsed+events, path);
        if (event < 0) return "";
        event += events;
        struct timeval timestamp = housesaga_getjsontime (EventParsed+event, "[0]");
        const char *category = housesaga_getjsonstring (EventParsed+event, "[1]");
        const char *object = housesaga_getjsonstring (EventParsed+event, "[2]");
        const char *action = housesaga_getjsonstring (EventParsed+event, "[3]");
        const char *description = housesaga_getjsonstring (EventParsed+event, "[4]");
        if ((timestamp.tv_sec > 0) &&
            host && app && category && object && action && description) {
            housesaga_event_new (&timestamp, host, app,
                                 category, object, action, description, 1);
        }
    }

    return "";
}

static const char *housesaga_webevents (const char *method, const char *uri,
                                        const char *data, int length) {

    if (!strcmp (method, "GET")) {
        return housesaga_webget ();
    } else { // Assume POST, PUT or anything with data.
        return housesaga_webpost (data, length);
    }
}

void housesaga_event_initialize (int argc, const char **argv) {

    if (!EventChronology) EventChronology = echttp_sorted_new();

    echttp_route_uri ("/saga/log/events", housesaga_webevents);
    echttp_route_uri ("/saga/log/latest", housesaga_weblatest); // Deprecated

    // Alternate paths for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/events", housesaga_webevents);
    echttp_route_uri ("/log/latest", housesaga_weblatest); // Deprecated.

    housesaga_event_background (time(0)); // Initial state.
}

void housesaga_event_background (time_t now) {

    static time_t LastCall = 0;
    if (now + 6 < LastCall) return;
    LastCall = now;

    housesaga_event_save (0);
}

