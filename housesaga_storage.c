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
 * housesaga_storage.c - The log disk storage module of HouseSaga.
 *
 * This module is responsible for saving the logs to permanent storage.
 *
 * SYNOPSYS:
 *
 * void housesaga_storage_save (const char *logtype, time_t timestamp,
 *                              const char *header, const char *record);
 *
 *    Save one CSV record to the specified log type file. The header, if
 *    not null, is written only once per file (if the file is empty).
 *
 * void housesaga_storage_flush (void);
 *
 *    Close the file and reset all stored context.
 *
 * void housesaga_storage_initialize (int argc, const char **argv);
 *
 *    Initialize the storage environment based on command line arguments.
 *
 * RESTRICTION
 *
 * For the sake of simplicity, this module opens only one file at a time.
 * All operations must follow the sequence:
 *    housesaga_storage_save ("mytype", ...);
 *    ...
 *    housesaga_storage_save ("mytype", ...);
 *    housesaga_storage_flush ();
 *
 * The flush operation will close the last file written. A flush operation
 * is forced if the log type changes.
 */

#include <unistd.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include "echttp.h"
#include "echttp_static.h"
#include "houselog.h"

#include "housesaga.h"
#include "housesaga_storage.h"

static const char *LogStorageFolder = "/var/lib/house/log";

static const char *LogStorageType = 0;
static FILE *LogStorageFile = 0;
static int LogStoragePeriod = 0;

static FILE *housesaga_storage_open (const char *logtype,
                                     int year, int month, int day) {

    int cursor;
    char path[1024];

    // Ignore all mkdir() errors: fopen() will fail anyway.
    //
    mkdir (LogStorageFolder, 0777);

    cursor = snprintf (path, sizeof(path), "%s/%04d", LogStorageFolder, year);
    mkdir (path, 0777);

    cursor += snprintf (path+cursor, sizeof(path)-cursor, "/%02d", month);
    mkdir (path, 0777);

    cursor += snprintf (path+cursor, sizeof(path)-cursor, "/%02d", day);
    mkdir (path, 0777);

    snprintf (path+cursor, sizeof(path)-cursor, "/%s.csv", logtype);
    return fopen (path, "a");
}

void housesaga_storage_save (const char *logtype, time_t timestamp,
                             const char *header, const char *record) {

    struct tm local = *localtime (&timestamp);
    int year = 1900 + local.tm_year;
    int month = local.tm_mon + 1;
    int day = local.tm_mday;
    int period = (year * 100 + month) * 100 + day; // Make a unique number.

    if (period != LogStoragePeriod) {
        housesaga_storage_flush ();
    } else if (LogStorageType) {
        if (strcmp (logtype, LogStorageType)) { // Switched type?
            housesaga_storage_flush (); // Don't mix data types in the same file
        }
    }
    LogStoragePeriod = period;
    LogStorageType = logtype;

    if (!LogStorageFile) {
        LogStorageFile = housesaga_storage_open (logtype, year, month, day);
        if (! LogStorageFile) return; // Hoops!
        if (header && (ftell (LogStorageFile) == 0)) {
            fprintf (LogStorageFile, "%s\n", header);
        }
    }
    fprintf (LogStorageFile, "%s\n", record);
}

void housesaga_storage_flush (void) {

    if (LogStorageFile) {
        fclose (LogStorageFile);
        LogStorageFile = 0;
    }
    LogStoragePeriod = 0;
    LogStorageType = 0;
}

void housesaga_storage_initialize (int argc, const char **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-log-path=", argv[i], &LogStorageFolder)) {
            houselog_trace (HOUSE_INFO, "PATH", "Log stored in %s", LogStorageFolder);
            continue;
        }
    }
    echttp_static_route ("/saga/archive", LogStorageFolder);
    echttp_static_route ("/archive", LogStorageFolder);
}

