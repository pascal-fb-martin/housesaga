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
 *    Save one CSV record to the specified log type file. The header is
 *    written only once per file.
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
#include "houselog.h"

#include "housesaga.h"
#include "housesaga_storage.h"

static const char *LogStorageFolder = "/var/lib/house/log";

static const char *LogStorageType = 0;
static FILE *LogStorageFile = 0;
static int LogStoragePeriod = 0;

static FILE *housesaga_storage_open (const char *logtype, int year, int month) {

    char path[512];
    char fullname[512];

    mkdir (LogStorageFolder, 0777); // Ignore error: fopen() will fail anyway.

    snprintf (path, sizeof(path), "%s/%04d", LogStorageFolder, year);
    mkdir (path, 0777); // Ignore error: fopen() will fail anyway.

    snprintf (path, sizeof(path),
              "%s/%04d/%02d", LogStorageFolder, year, month);
    mkdir (path, 0777); // Ignore error: fopen() will fail anyway.

    snprintf (fullname, sizeof(fullname), "%s/%s.csv", path, logtype);
    return fopen (fullname, "a");
}

void housesaga_storage_save (const char *logtype, time_t timestamp,
                             const char *header, const char *record) {

    if (LogStorageType) {
        if (strcmp (logtype, LogStorageType)) { // Switched type?
            housesaga_storage_flush (); // Don't mix data types in the same file
        }
    }
    LogStorageType = logtype;

    struct tm local = *localtime (&timestamp);
    int year = 1900 + local.tm_year;
    int month = local.tm_mon + 1;
    int period = year * 100 + month; // All we want is something unique.

    if (period != LogStoragePeriod) {
        if (LogStorageFile) {
            fclose (LogStorageFile);
            LogStorageFile = 0;
        }
        LogStoragePeriod = period;
    }
    if (!LogStorageFile) {
        LogStorageFile = housesaga_storage_open (logtype, year, month);
        if (! LogStorageFile) return; // Hoops!
        if (ftell (LogStorageFile) == 0) {
            fprintf (LogStorageFile, "%s\n", header);
        }
    }
    fprintf (LogStorageFile, "%s\n", record);
}

void housesaga_storage_flush (void) {

    if (!LogStorageType) return;
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
}

