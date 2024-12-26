/* housesaga - A log consolidation and storage service.
 *
 * Copyright 2024, Pascal Martin
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
 * housesaga_metrics.c - The metrics consolidation module of HouseSaga.
 *
 * This module is responsible for storing metrics objects to disk.
 * Metrics are JSON objects that are written as-is to disk.
 *
 * SYNOPSYS:
 *
 * void housesaga_metrics_initialize (int argc, const char **argv);
 *
 *    Initialize the environment required to consolidate metrics logs.
 */

#include <string.h>
#include <time.h>

#include "echttp.h"

#include "housesaga.h"
#include "housesaga_metrics.h"
#include "housesaga_storage.h"

static const char *housesaga_webmetrics (const char *method, const char *uri,
                                         const char *data, int length) {

    if (strcmp (method, "POST")) return ""; // Only POST is supported.

    housesaga_storage_save ("metrics", time(0), 0, data);
    housesaga_storage_flush ();

    return "";
}

void housesaga_metrics_initialize (int argc, const char **argv) {

    echttp_route_uri ("/saga/log/metrics", housesaga_webmetrics);

    // Alternate path for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/metrics", housesaga_webmetrics);
}

