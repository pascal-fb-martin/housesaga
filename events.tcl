#!/usr/bin/tclsh
# HouseSaga - a log storage service.
#
# Copyright 2024, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# events - A tools to convert log timestamps into a user-friendly format.
#
# BUGS
#
#    This tool is not terribly fast when the log file is large.

# This is the default storage location.
set logroot /var/lib/house/log

set filetype event.csv
set dateargv {}

foreach option $argv {
   if {[string match "X-h" "X$option"]} {
      puts "[lindex argv 0] \[-e|-s|-t\] \[year \[month \[day\]\]\]"
      puts "  -e     Show events (default)"
      puts "  -s     Show sensor data"
      puts "  -t     Show traces"
      exit 0
   }
   if {[string match "X-e" "X$option"]} {set filetype event.csv}
   if {[string match "X-s" "X$option"]} {set filetype sensor.csv}
   if {[string match "X-t" "X$option"]} {set filetype trace.csv}
   if {[string match {X[0-9]*} "X$option"]} {lappend dateargv [format {%02d} $option]}
}
set argv $dateargv
set now [clock seconds]

if {[llength $argv] <= 0} {
    set year [clock format $now -format "%Y"]
    set month [clock format $now -format "%m"]
    set day [clock format $now -format "%d"]
    set localpath [file join $year $month $day]
} else {
    set year [lindex $argv 0]
    set localpath [file join $year]

    if {[llength $argv] > 1} {
        set month [lindex $argv 1]
        set localpath [file join $year $month]
    }
    if {[llength $argv] > 2} {
        set day [lindex $argv 2]
        set localpath [file join $year $month $day]
    }
}
# First plan to search at the default location..
set searchpath [file join $logroot $localpath]

# A personal convention is to store events on a large secondary storage
# (as <mount point>/house/log) when running on a Raspberry Pi, where
# the boot storage is a Micro SD that should not be continuously written to.
# Only use that location if it has the data being looked for.
#
set volums [split [exec /usr/bin/df --output=target --exclude-type=tmpfs --exclude-type=devtmpfs] "\n"]

foreach mount $volums {
    if {[string match {Mounted on*} $mount]} continue
    set location [file join $mount house log $localpath]
    if {[file isdirectory $location]} {
        set searchpath $location
        break
    }
}

proc convertcsv {name} {
   set fd [open $name r]
   gets $fd
   while {![eof $fd]} {
      set line [gets $fd]
      if {$line == {}} continue
      set data [split $line {,}]
      set timestamp [split [lindex $data 0] {.}]
      if {[llength $timestamp] != 2} continue
      set seconds [lindex $timestamp 0]
      set output [join [concat [list "[clock format $seconds -format {%D %T}].[lindex $timestamp 1]"] [lrange $data 1 end]] {,}]

      if {[catch "puts {$output}"]} exit
   }
   close $fd
}

if {[file isdirectory $searchpath]} {
    set dirlist [lsort [exec find $searchpath -type d]]
    foreach d $dirlist {
       if {[file exists [file join $d $filetype]]} {
           convertcsv [file join $d $filetype]
       }
   }
}

