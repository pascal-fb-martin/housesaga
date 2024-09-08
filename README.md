# HouseSaga
A House web service to maintain consolidated logs from all services.
## Overview
This is a web server application that consolidate events and stores them to
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

By convention there are three types of logs commonly used with HouseSaga:
* Event logs.
* Trace logs.
* Measure logs. (not supported for now)
* Metrics logs. (not supported for now)

Note that this service consolidates records from all sources into a single log for each type of log. There is one single event log aggregating events from all sources, one single trace log aggregating traces from all sources, etc.

All logs share a few common properties:
* Use the CSV format (text/csv). The first line contains the names of columns.
* One field is named "TIMESTAMP" and contains a UNIX system time in the format ssssss[.mmm].

A measure log is a set of records where each record contains an identifier, a value and a unit fields (optional). These are used to collect some real-world external measurements. A data collection csv record contains at least the following field:
- TIMESTAMP,
- LOCATION,
- NAME,
- VALUE,
- UNIT.

The value of UNIT may be empty.

An event log records an history of status and actions that are related to the purpose of the service. Interpreting an event should not require any knowledge of the application's implementation, only an understanding of the purpose and configuration of the service. The event log is relevant to the user of the service. An event csv record contains at least the following fields:
- TIMESTAMP,
- HOST,
- APP,
- OBJECT
- CATEGORY,
- ACTION,
- DESCRIPTION.

A trace log records an history of status and actions that describe how, and how well, the service is fulfilling its purpose. The trace log is relevant to the developper of the application, or the system administrator of the service. A trace csv record contains at least the following fields:
- TIMESTAMP,
- HOST,
- APP,
- FILE,
- LINE,
- OBJECT,
- LEVEL,
- DESCRIPTION.

If there are additional fields compare to what is described above, these fields will be stored, but not used by the HouseSaga's web interface.

More types of logs can be used, but may not be visualized in the the HouseSaga's web interface.

If multiple HouseSaga services are active, the client services should transmit their logs to all detected, on a best effort basis. This means that if one HouseSaga service fails and then restarts, it might be missing some logs. As long as not all HouseSaga services failed, the data will have been saved at least once. It might be necessary to query multiple HouseSaga services to recover all log data.

HouseSaga stores all log files in /var/lib/house/log. The directory organization below that directory matches the HTTP URL.

## Web API for Events

```
GET /saga/log/events[?from=<timestamp>]
```
Retrieve up to 256 of the most recent events, or up to 256 of the event starting at timestamp. The events are shown in reverse chronological order (most recent event first). Only events still stored in RAM can be accessed this way.

```
POST /saga/log/events
```
Push a new list of events to HouseSaga. The format of the events is the same as the JSON format provided for the source's own web event page.

## Web API for Other Log Types

```
GET /saga/log/<type>/<year>/<month>.csv
```

Retrieve one log file. By convention the path contains the type of data, then the name of the service and server that produced the log. The path must end with the 4-digits year and 2-digits month number (from 01 to 12). Some services may generate more than one type of log.

```
POST /saga/log/<type>
```

The data associated must start with a field name line, followed by CSV records.

Each POST appends more records to the log. HouseSaga will infer the year and month from each record timestamps, not from the time of the submission. Therefore a TIMESTAMP field is mandatory in the CSV data.

## Configuration

There is no user configuration file.

