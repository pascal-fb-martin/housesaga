# HouseSaga - a log storage service.
#
# Copyright 2023, Pascal Martin
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

HAPP=housesaga
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS= housesaga.o housesaga_event.o housesaga_trace.o housesaga_storage.o
LIBOJS=

all: housesaga

clean:
	rm -f *.o *.a housesaga

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housesaga: $(OBJS)
	gcc -g -O -o housesaga $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lrt

# Application installation. -------------------------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housesaga
	cp housesaga $(HROOT)/bin
	chown root:root $(HROOT)/bin/housesaga
	chmod 755 $(HROOT)/bin/housesaga
	mkdir -p $(SHARE)/public/saga
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/saga
	cp public/* $(SHARE)/public/saga
	chown root:root $(SHARE)/public/saga/*
	chmod 644 $(SHARE)/public/saga/*
	touch /etc/default/housesaga
	mkdir -p /var/lib/house/config /var/lib/house/scripts
	chmod 755 /var/lib/house/config /var/lib/house/scripts

uninstall-app:
	rm -rf $(SHARE)/public/saga
	rm -f $(HROOT)/bin/housesaga

purge-app:

purge-config:
	rm -rf /etc/default/housesaga

# System installation. ------------------------------------------

include $(SHARE)/install.mak

