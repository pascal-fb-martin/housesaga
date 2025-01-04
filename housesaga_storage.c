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
#include <dirent.h>

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

    if (strchr (logtype, '.')) {
        snprintf (path+cursor, sizeof(path)-cursor, "/%s", logtype);
    } else {
        snprintf (path+cursor, sizeof(path)-cursor, "/%s.csv", logtype);
    }
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

static const char *saga_storage_monthly (const char *method, const char *uri,
                                         const char *data, int length) {

    int i;
    static char buffer[2048];

    const char *year = echttp_parameter_get("year");
    const char *month = echttp_parameter_get("month");
    int cursor = 0;

    if (!year || !month) {
        echttp_error (404, "Not Found");
        return "";
    }
    if (month[0] == '0') month += 1;

    struct tm local;
    // The reference time must be slightly past 2 AM to avoid being fooled
    // by a daylight saving time change in the fall.
    local.tm_sec = local.tm_min = local.tm_hour = 2; // 2:02:02 AM
    local.tm_mday = 1;
    local.tm_mon = atoi(month)-1;
    local.tm_year = atoi(year)-1900;
    local.tm_isdst = -1;
    time_t base = mktime (&local);
    if (base < 0) {
        echttp_error (404, "Not Found");
        return "";
    }

    cursor = snprintf (buffer, sizeof(buffer), "[false");

    int referencemonth = local.tm_mon;
    struct stat info;
    char path[1024];
    int  tail = snprintf (path, sizeof(path), "%s/%s/%02d/",
                          LogStorageFolder, year, local.tm_mon+1);

    for (i = 1; i <= 31; ++i) {
        const char *found = ",false";
        snprintf (path+tail, sizeof(path)-tail, "%02d", local.tm_mday);
        if (!stat (path, &info)) {
            if ((info.st_mode & S_IFMT) == S_IFDIR) {
                found = ",true";
            }
        }
        cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, found);
        if (cursor >= sizeof(buffer)) goto nospace;

        base += 24*60*60;
        local = *localtime (&base);
        if (local.tm_mon != referencemonth) break;
    }
    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, "]");
    if (cursor >= sizeof(buffer)) goto nospace;
    echttp_content_type_json();
    return buffer;

nospace:
    echttp_error (413, "Out Of Space");
    return "HTTP Error 413: Out of space, response too large";
}

static const char *saga_storage_daily (const char *method, const char *uri,
                                       const char *data, int length) {
    static char buffer[131072];

    char path[1024];
    int  tail;
    char filepath[1024];
    struct stat info;

    int  cursor;
    const char *year = echttp_parameter_get("year");
    const char *month = echttp_parameter_get("month");
    const char *day = echttp_parameter_get("day");

    if (month[0] == '0') month += 1;
    if (day[0] == '0') day += 1;

    snprintf (path, sizeof(path), "%s/%s/%02d/%02d",
              LogStorageFolder, year, atoi(month), atoi(day));
    DIR *dir = opendir (path);
    if (!dir) {
        echttp_error (404, "Not Found");
        return "";
    }
    tail = snprintf (filepath, sizeof(filepath),
                     "%s/%02d/%02d/", year, atoi(month), atoi(day));

    const char *sep = "";
    cursor = snprintf (buffer, sizeof(buffer), "[");

    for (;;) {
        struct dirent *p = readdir(dir);
        if (!p) break;
        if (p->d_name[0] == '.') continue;

        cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                            "%s\"%s%s\"", sep, filepath, p->d_name);
        sep = ",";
        if (cursor >= sizeof(buffer)) goto nospace;
    }

    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, "]");
    if (cursor >= sizeof(buffer)) goto nospace;

    closedir(dir);
    echttp_content_type_json();
    return buffer;

nospace:
    closedir(dir);
    echttp_error (413, "Out Of Space");
    return "HTTP Error 413: Out of space, response too large";
}

void housesaga_storage_initialize (int argc, const char **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-log-path=", argv[i], &LogStorageFolder)) {
            houselog_trace (HOUSE_INFO, "PATH", "Log stored in %s", LogStorageFolder);
            continue;
        }
    }
    echttp_route_uri ("/saga/monthly", saga_storage_monthly);
    echttp_route_uri ("/saga/daily", saga_storage_daily);
    echttp_static_route ("/saga/archive", LogStorageFolder);

    echttp_route_uri ("/monthly", saga_storage_monthly);
    echttp_route_uri ("/daily", saga_storage_daily);
    echttp_static_route ("/archive", LogStorageFolder);
}

