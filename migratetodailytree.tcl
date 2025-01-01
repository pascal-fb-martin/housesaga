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
# migratetodaily - A tools to convert monthly logs to daily logs.
#
# BUGS
#
#    PLEASE STOP THE HOUSESAGA SERVICE FIRST.
#
#    This tool is not terribly fast when the logs file are large.

set logroot /var/lib/house/log

set now [clock seconds]

set CurrentYear [clock format $now -format "%Y"]
set CurrentMonth [clock format $now -format "%m"]


proc splitcsv {path name} {
   set filename [file join $path $name]
   puts "      processing file $filename"
   set f [open $filename]
   set header [gets $f]
   set day 0
   while {![eof $f]} {
      set line [gets $f]
      if {$line == {}} continue
      set data [split $line {,}]
      set timestamp [lindex [split [lindex [split $line {,}] 0] {.}] 0]
      if {[string match {[0-9]*} $timestamp]} {
         set newday [clock format $timestamp -format {%d}]
         if {$newday != $day} {
             if {$day > 0} {
                 close $t
                 exec chown house:house $output
             }
             set day $newday
             set newdir [file join $path $day]
             set output [file join $newdir $name]
             if {![file isdirectory $newdir]} {
                puts "         creating directory $newdir"
                file mkdir $newdir
                exec chown house:house $newdir
             }
             puts "         writing file $output"
             set t [open $output w]
             puts $t $header
         }
      }
      puts $t $line
    }
    if {$day > 0} {close $t}
    close $f
}

set startmonth $CurrentMonth

for {set year $CurrentYear} {$year > 2000} {incr year -1} {
    puts "Scanning year $year .."
    if {[file isdirectory [file join $logroot $year]]} {
        for {set month $startmonth} {$month >= 1} {incr month -1} {
            set path [file join $logroot $year [format {%02d} $month]]
            if {[file isdirectory $path]} {
                puts "   found month $month"
                foreach name [list event.csv sensor.csv trace.csv] {
                    set monthly [file join $path $name]
                    if {[file exists $monthly]} {
                        splitcsv $path $name
                        file delete $monthly
                    }
                }
            }
        }
        set startmonth 12
    }
}

