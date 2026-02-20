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
 * housesaga_traffic.c - Provide data traffic information.
 *
 * This module is responsible for maintaining local traffic rates and serving
 * them to web clients.
 *
 * SYNOPSYS:
 *
 * void housesaga_traffic_initialize (int argc, const char **argv);
 *
 *    Initialize the environment required to calculate traffic rates. This
 *    must be the first function that the application calls.
 *
 * void housesaga_traffic_increment (const char *id);
 *
 *    Record new traffic.
 *
 * void housesaga_traffic_background (time_t now);
 *
 *    Periodic cleanup of the traffic rate data.
 *
 * NOTE:
 *
 *    The rates represent 10 seconds of activity.
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
#include "echttp_json.h"
#include "houselog.h"

#include "housesaga.h"
#include "housesaga_traffic.h"

#define SAGASTAT_PERIOD 10
struct SagaStat {
    const char *id;
    long values[SAGASTAT_PERIOD];
    time_t cleanup;
};

#define SAGASTAT_MAX 32
static struct SagaStat SagaValues[SAGASTAT_MAX];
static int SagaValuesCount = 0;

void housesaga_traffic_cleanup (int i, time_t now) {

    if (SagaValues[i].cleanup > now) return; // Not needed yet.

    // Don't walk the array more than once.
    if (SagaValues[i].cleanup < now - SAGASTAT_PERIOD)
        SagaValues[i].cleanup = now - SAGASTAT_PERIOD;

    for ( ; SagaValues[i].cleanup <= now; SagaValues[i].cleanup += 1) {
        SagaValues[i].values[SagaValues[i].cleanup%SAGASTAT_PERIOD] = 0;
    }
}

void housesaga_traffic_increment (const char *id) {

    time_t now = time(0);
    int i;
    for (i = 0; i < SagaValuesCount; ++i) {
       if (!strcasecmp (id, SagaValues[i].id)) {
          housesaga_traffic_cleanup (i, now);
          SagaValues[i].values[now%SAGASTAT_PERIOD] += 1;
          return;
       }
    }
    // This is a new traffic item.
    if (SagaValuesCount >= SAGASTAT_MAX) return; // Full.
    SagaValuesCount += 1;
    SagaValues[i].id = id;
    int j;
    for (j = 0; j < SAGASTAT_PERIOD; ++j) {
       SagaValues[i].values[j] = 0;
    }
    SagaValues[i].values[now%SAGASTAT_PERIOD] += 1;
    SagaValues[i].cleanup = now + 1;
}

static const char *housesaga_traffic_status (const char *method,
                                           const char *uri,
                                           const char *data, int length) {

    if (strcmp (method, "GET")) return ""; // Only GET is supported.

    static char buffer[65537];
    ParserToken token[1024];
    char pool[65537];

    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", housesaga_host());
    echttp_json_add_integer (context, root, "timestamp", (long long)time(0));
    int top = echttp_json_add_object (context, root, "saga");
    int container = echttp_json_add_array (context, top, "traffic");

    int i;
    for (i = 0; i < SagaValuesCount; ++i) {
        int item = echttp_json_add_object (context, container, 0);
        echttp_json_add_string (context, item, "id", SagaValues[i].id);
        int total = SagaValues[i].values[0];
        int j;
        for (j = 1; j < SAGASTAT_PERIOD; ++j) {
            total += SagaValues[i].values[j];
        }
        echttp_json_add_integer (context, item, "value", total);
    }

    const char *error = echttp_json_export (context, buffer, sizeof(buffer));
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

void housesaga_traffic_background (time_t now) {

    int i;
    for (i = 0; i < SagaValuesCount; ++i) {
        housesaga_traffic_cleanup (i, now);
    }
}

void housesaga_traffic_initialize (int argc, const char **argv) {

    echttp_route_uri ("/saga/log/traffic", housesaga_traffic_status);

    // Alternate path for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/traffic", housesaga_traffic_status);
}

