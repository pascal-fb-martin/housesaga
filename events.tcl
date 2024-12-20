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

set logroot /var/lib/house/log

set now [clock seconds]

set year [clock format $now -format "%Y"]
set month [clock format $now -format "%m"]
set path [file join $logroot $year $month]

set filetype event.csv

foreach option $argv {
   if {[string match "X$option" "X-e"]} {set filetype event.csv}
   if {[string match "X$option" "X-s"]} {set filetype sensor.csv}
   if {[string match "X$option" "X-t"]} {set filetype trace.csv}
}
set filepath [file join $path $filetype]

if {[file exist $filepath]} {
   set fd [open $filepath r]
   gets $fd
   while {![eof $fd]} {
      set line [gets $fd]
      if {$line == {}} continue
      set data [split $line {,}]
      set timestamp [split [lindex $data 0] {.}]
      if {[llength $timestamp] != 2} continue
      set seconds [lindex $timestamp 0]
      puts [join [concat [list "[clock format $seconds -format {%D %T}].[lindex $timestamp 1]"] [lrange $data 1 end]] {,}]
   }
} else {
   puts "No event for [clock format $now -format {%B %Y}]"
}

