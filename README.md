# HouseSaga
A House web service to maintain consolidated logs from all services.
## Overview
This is a web server application that consolidate events, sensor data and traces and stores them to
files for other web services.

The service implements a specific behavior:

- Any update is additive. Existing data cannot be modified.
- Log files are timestamped CSV records.

HouseSaga is concerned about safeguarding history data on behalf of client services. It is not really meant to be accessed directly by end-users. It however offers a web UI showing the consolidated events, mostly for troubleshooting purposes.

The goal is to provide a simple Web API (GET and POST methods), with no extensions beside a few optional parameters, that implement a specified behavior based on URL matching (see below for a description of the web API).

It is the intent of the design to allow multipe HouseSaga services to run concurrently, on separate machines, for redundancy. The clients are responsible for sending updates to all active HouseSaga services: there is no synchronization between HouseSaga services.

Access to HouseSaga is not restricted: this service should only be accessible from a private network, and the data should not be sensitive. Do not store cryptographic keys using HouseSaga.

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install

## Log Files

HouseSaga comes with a command line tool named `houseevents` that makes it easier to read the logs: it knows the (default) root directory for the log files, it selects the log file for the current month and it converts all numeric timestamps to a readable date and time format. This tool syntax is:

```
   houseevents [-e|-s|-t] [year [month [day]]]
```
The options are:

* -e: show events. (Default)
* -s: show sensor data.
* -t: show traces.

If no year is provided, the default is the current day. If only the year is provided, day and month default to 12/31. If only the year and month are provided, the day will default to 31 (I know..).

Warning: this tool requires Tcl. It cannot process metrics logs.

By convention there are several types of logs commonly used with HouseSaga:

* Event logs.
* Trace logs.
* Sensor measurement logs.
* Metrics logs.

Note that this service consolidates records from all sources into a single daily file for each type of log. There is one daily event log aggregating events from all sources, one daily trace log aggregating traces from all sources, etc.

All log files are organized in daily sets, i.e. a file is created each day in a _year_/_month_/_day_ subdirectory. The default root of that tree is /var/lib/house/log.

The event, trace and sensor logs share a few common properties:

* Use the CSV format (text/csv). The first line contains the names of columns.
* One field is named "TIMESTAMP" and contains a UNIX system time in the format ssssss[.mmm].

The value of UNIT may be empty.

An event log records an history of status and actions that are related to the purpose of the service. Interpreting an event should not require any knowledge of the application's source code, only an understanding of the purpose and user configuration of the service. The event log is relevant to the user of the service. An event csv record contains at least the following fields:

- TIMESTAMP,
- HOST,
- APP,
- OBJECT
- CATEGORY,
- ACTION,
- DESCRIPTION.

A trace log records an history of status and actions that describe how, and how well, the service is fulfilling its purpose. The trace log is relevant to the developer of the application, or to the system administrator of the service. A trace csv record contains at least the following fields:

- TIMESTAMP,
- HOST,
- APP,
- FILE,
- LINE,
- OBJECT,
- LEVEL,
- DESCRIPTION.

A sensor log is a set of records where each record contains an identifier, a value and a unit fields (optional). These are used to collect some real-world external measurements. A data collection csv record contains at least the following field:

- TIMESTAMP,
- LOCATION,
- NAME,
- VALUE,
- UNIT.

If there are additional fields compare to what is described above, these fields will be stored, but not used by the HouseSaga's web interface.

The metrics log is organized differently, as a sequence of JSON objects.

More types of logs can be used, but may not be visualized in the the HouseSaga's web interface.

If multiple HouseSaga services are active, the client services should transmit their logs to all detected, on a best effort basis. This means that if one HouseSaga service fails and then restarts, it might be missing some logs. As long as not all HouseSaga services failed, the data will have been saved at least once. It might be necessary to query multiple HouseSaga services to recover all log data.

## Web API

### web API for Archive

```
GET /saga/monthly?year=<number>&month=<number>
```

Returns a JSON array, one element per day of the requested month. Each element is a boolean: true if there were files archived for this day, false otherwise. The array has one more element than days in this month: element at index 0 is always false and should not be used.

```
GET /saga/daily?year=<number>&month=<number>&day=<number>
```

Returns a JSON array with the list of archive files for the requested day, using a relative path _year_/_month_/_day_/_file_.

```
GET /saga/archive/<year>/<month>/<day>/event.csv
GET /saga/archive/<year>/<month>/<day>/trace.csv
GET /saga/archive/<year>/<month>/<day>/sensor.csv
```

Access the specified log file. (This does not work for metrics--for now.)

### Web API for Events

```
GET /saga/log/latest
```
Retrieve an ID of the latest event. Whatever this ID represents, or how it is built, is irrelevant. The only point is that this value changes when new events have been recorded. A client may poll this URI periodically: if any new event is detected, then the client must call the subsequent URI to retrieve the new list of events.

```
GET /saga/log/events
```
Retrieve up to 256 of the most recent events. The events are shown in reverse chronological order (most recent event first). Only events still stored in RAM can be accessed this way.

```
POST /saga/log/events
```
Push a new list of events to HouseSaga. The format of the events is the same as the JSON format provided for the source's own web event page.

Each POST appends more events to the log. HouseSaga will infer the year and month from each event timestamps, not from the time of the submission. Therefore a timestamp field is mandatory in each event.

### Web API for Traces

```
POST /saga/log/traces
```
Push a new list of traces to HouseSaga. These traces are immediately written to storage. This means that traces might be stored out of their original sequence, depending on the clients buffer mechanisms.

### Web API for sensor data

```
GET /saga/log/sensor/check
```
Retrieve an ID of the latest sensor data. Whatever this ID represents, or how it is built, is irrelevant. The only point is that this value changes when new sensor data has been recorded. A client may poll this URI periodically: if any new sensor data is detected, then the client must call the subsequent URI to retrieve the new list of sensor data.

```
GET /saga/log/sensor/data
```

Retrieve the most recent sensor data. The data is listed in reverse chronological order (most recent data first). Only sensor data still stored in RAM is accessible.

```
POST /saga/log/sensor/data
```

Push a new list of events to HouseSaga, in JSON format.

Each POST appends more sensor records to the log. HouseSaga will infer the year and month from each record timestamps, not from the time of the submission. Therefore a timestamp field is mandatory in each record.

### Web API for Metrics

```
POST /saga/log/metrics
```
Push one more metrics JSON object to the log. HouseSaga does not decode or validate the JSON object's content.

## Configuration

There is no user configuration file.

