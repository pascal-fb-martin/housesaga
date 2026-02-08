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
 * housesaga_sensor.c - The sensor data log consolidation module of HouseSaga.
 *
 * This is responsible for keeping the latest sensor data in memory. This also
 * call the HouseSaga storage module when either of the conditions below
 * is met:
 *  - the buffer is full and the oldest item was not saved to storage.
 *  - there is at least one unsaved item and last save was N seconds ago.
 *
 * The origin of sensor data are source clients, which report their new,
 * unreported data on their own. Every record must include the following items:
 *
 *  - Timestamp (integer)
 *  - Sensor location (string)
 *  - Sensor ID (string),
 *  - Value (numeric or string)
 *  - Unit (quoted string)
 *
 * The data is stored in RAM in the order it was received. However this
 * does not necessarily match the chronological order of data, because of
 * source buffering and flush delays. This is why a sorted list storage is
 * used.
 *
 * SYNOPSYS:
 *
 * void housesaga_sensor_initialize (int argc, const char **argv);
 *
 *    Initialize the environment required to consolidate event logs. This
 *    must be the first function that the application calls.
 *
 * void housesaga_sensor_background (time_t now);
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
#include "housesaga_sensor.h"
#include "housesaga_storage.h"

static const char  LogAppName[] = "saga";

struct SensorRecord {
    struct timeval timestamp;
    long long id;
    int    unsaved;
    char   host[128];
    char   app[128];
    char   location[32];
    char   name[32];
    char   value[16];
    char   unit[16];
};

#define HISTORY_DEPTH 256

static struct SensorRecord SensorHistory[HISTORY_DEPTH];
static int SensorCursor = 0;
static long long SensorLatestId = 0;

static echttp_sorted_list SensorChronology;
static time_t SensorLastSaved = 0;
static time_t SensorSaveLimit = 0;

static time_t WebFormatSinceSec = 0;
static int WebFormatSinceUSec = 0;


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

    static char SensorHeader[] =
        "TIMESTAMP,HOST,APP,LOCATION,NAME,VALUE,UNIT";

    struct SensorRecord *cursor = SensorHistory + (intptr_t) data;

    if (cursor->unsaved) {
        char buffer[1024];

        if (cursor->timestamp.tv_sec > SensorSaveLimit) return 0;

        snprintf (buffer, sizeof(buffer), "%lld.%03d,%s,%s,%s,%s,%s,%s",
                  (long long)(cursor->timestamp.tv_sec),
                  (int)(cursor->timestamp.tv_usec / 1000),
                  cursor->host,
                  cursor->app,
                  cursor->location,
                  cursor->name,
                  cursor->value,
                  cursor->unit);
        housesaga_storage_save ("sensor", cursor->timestamp.tv_sec,
                                SensorHeader, buffer);
        cursor->unsaved = 0;
    }
    return 1;
}

static void housesaga_sensor_save (int full) {

    time_t now = time(0);

    // In order to keep the historical log in chronological order, we delay
    // storing recent events as they could have not been all reported yet,
    // due to bufferization by the source. The delay here gives enough time
    // for all sources to have flushed their events out.
    // This delay does not apply when the event buffer is full: in that
    // case, we must save events at all cost.
    //
    SensorSaveLimit = full ? now + 2 : now - 6;

    if (SensorLastSaved) {
        echttp_sorted_ascending_from (SensorChronology,
                                      SensorLastSaved * 1000,
                                      housesaga_saveaction);
    } else {
        echttp_sorted_ascending (SensorChronology, housesaga_saveaction);
    }
    housesaga_storage_flush();
    SensorLastSaved = full ? now : SensorSaveLimit;
}

/* Record a new data record to the live buffer.
 */
static void housesaga_sensor_new (const struct timeval *timestamp,
                                  const char *host,
                                  const char *app,
                                  const char *location,
                                  const char *name,
                                  const char *value,
                                  const char *unit) {

    struct SensorRecord *cursor = SensorHistory + SensorCursor;

    if (!SensorChronology) SensorChronology = echttp_sorted_new();

    if (SensorLatestId == 0) {
        // Seed the latest sensor data ID based on the current time.
        // This makes it random enough to make its value change after
        // a restart.
        SensorLatestId = (long long) (time(0) & 0xfffff);
    }
    SensorLatestId += 1;

    cursor->timestamp = *timestamp;
    cursor->id = SensorLatestId;
    safecpy (cursor->host, host, sizeof(cursor->host));
    safecpy (cursor->app, app, sizeof(cursor->app));
    safecpy (cursor->location, location, sizeof(cursor->location));
    safecpy (cursor->name, name, sizeof(cursor->name));
    safecpy (cursor->value, value, sizeof(cursor->value));
    safecpy (cursor->unit, unit, sizeof(cursor->unit));
    cursor->unsaved = 1;

    echttp_sorted_add (SensorChronology,
                       housesaga_timestamp2key (&(cursor->timestamp)),
                       (void *)((long)SensorCursor));

    if (timestamp->tv_sec < SensorLastSaved) {
        // Hoops: we got a late data from a distant past. We need
        // to make sure it will be saved, even if out of order.
        SensorLastSaved = timestamp->tv_sec;
    }

    SensorCursor += 1;
    if (SensorCursor >= HISTORY_DEPTH) SensorCursor = 0;

    cursor = SensorHistory + SensorCursor;
    if (cursor->timestamp.tv_sec) {
        if (cursor->unsaved) housesaga_sensor_save(1); // Save before erased.

        echttp_sorted_remove (SensorChronology,
                              housesaga_timestamp2key (&(cursor->timestamp)),
                              (void *)((long)SensorCursor));
        cursor->timestamp.tv_sec = 0;
    }
}

static int housesaga_sensor_getheader (char *buffer, int size, const char *from) {

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
                    (long long)time(0), SensorLatestId, LogAppName, fromparam, SensorLatestId);
    // (The second iteration of SensorLatestId above is for compatibility only.)
}

static char WebFormatBuffer[128+HISTORY_DEPTH*(sizeof(struct SensorRecord)+24)] = {0};
static int WebFormatLength = 0;
static const char *WebFormatPrefix = "";

static int housesaga_webaction (void *data) {

    int size = sizeof(WebFormatBuffer) - 4; // Need room to complete the JSON.

    struct SensorRecord *cursor = SensorHistory + (intptr_t) data;

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
                          cursor->location,
                          cursor->name,
                          cursor->value,
                          cursor->unit,
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

// This request is deprecated. Use the "GET /log/sensor/data" request with
// the "known" parameter instead for a more efficient polling.
//
static const char *housesaga_weblatest (const char *method, const char *uri,
                                        const char *data, int length) {

    static char buffer[256];
    int written = housesaga_sensor_getheader (buffer, sizeof(buffer), 0);
    snprintf (buffer+written, sizeof(buffer)-written, "}}");
    return buffer;
}

static const char *housesaga_webget (void) {

    const char *known = echttp_parameter_get("known");
    if (known && (atoll (known) == SensorLatestId)) {
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

    WebFormatLength = housesaga_sensor_getheader (WebFormatBuffer,
                                                sizeof(WebFormatBuffer), 0);
    WebFormatLength += snprintf (WebFormatBuffer+WebFormatLength,
                                 sizeof(WebFormatBuffer)-WebFormatLength,
                                 ",\"sensor\":[");

    WebFormatPrefix = "";
    echttp_sorted_descending(SensorChronology, housesaga_webaction);
    snprintf (WebFormatBuffer+WebFormatLength,
              sizeof(WebFormatBuffer)-WebFormatLength, "]}}");
    return WebFormatBuffer;
}

/* Decode a report of data from a source client.
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

    static ParserToken *SensorParsed = 0;
    static int   SensorTokenAllocated = 0;
    static char *SensorBuffer = 0;

    echttp_content_type_json ();

    if (SensorBuffer) free (SensorBuffer);
    SensorBuffer = strdup (data);

    int count = echttp_json_estimate(data);
    if (count > SensorTokenAllocated) {
        if (SensorParsed) free (SensorParsed);
        SensorTokenAllocated = count;
        SensorParsed = calloc (count, sizeof(ParserToken));
    }
    const char *error = echttp_json_parse (SensorBuffer, SensorParsed, &count);
    if (error) return ""; // Ignore bad data from applications.

    // TBD: decode JSON, register events.
    const char *host = housesaga_getjsonstring (SensorParsed, ".host");
    const char *app = housesaga_getjsonstring (SensorParsed, ".apps[0]");

    char path[128];
    snprintf (path, sizeof(path), ".%s.sensor", app);
    int events = echttp_json_search(SensorParsed, path);
    if (SensorParsed[events].type != PARSER_ARRAY) return "";

    int i;
    for (i = 0; i < SensorParsed[events].length; ++i) {
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        int event = echttp_json_search(SensorParsed+events, path);
        if (event < 0) return "";
        event += events;
        struct timeval timestamp = housesaga_getjsontime (SensorParsed+event, "[0]");
        const char *location = housesaga_getjsonstring (SensorParsed+event, "[1]");
        const char *name = housesaga_getjsonstring (SensorParsed+event, "[2]");
        const char *value = housesaga_getjsonstring (SensorParsed+event, "[3]");
        const char *unit = housesaga_getjsonstring (SensorParsed+event, "[4]");
        if ((timestamp.tv_sec > 0) &&
            host && app && location && name && value && unit) {
            housesaga_sensor_new
                (&timestamp, host, app, location, name, value, unit);
        }
    }

    return "";
}

static const char *housesaga_websensor (const char *method, const char *uri,
                                        const char *data, int length) {

    if (!strcmp (method, "GET")) {
        return housesaga_webget ();
    } else { // Assume POST, PUT or anything with data.
        return housesaga_webpost (data, length);
    }
}

void housesaga_sensor_initialize (int argc, const char **argv) {

    if (!SensorChronology) SensorChronology = echttp_sorted_new();

    echttp_route_uri ("/saga/log/sensor/data", housesaga_websensor);
    echttp_route_uri ("/saga/log/sensor/latest", housesaga_weblatest); // Deprecated
    echttp_route_uri ("/saga/log/sensor/check", housesaga_weblatest); // Compatibility.

    // Alternate paths for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/sensor/data", housesaga_websensor);
    echttp_route_uri ("/log/sensor/latest", housesaga_weblatest); // Deprecated
    echttp_route_uri ("/log/sensor/check", housesaga_weblatest); // Compatibility.

    housesaga_sensor_background (time(0)); // Initial state.
}

void housesaga_sensor_background (time_t now) {

    static time_t LastCall = 0;
    if (now + 6 < LastCall) return;
    LastCall = now;

    housesaga_sensor_save (0);
}

