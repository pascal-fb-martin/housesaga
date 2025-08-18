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
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housesaga
HCAT=infrastructure
STORE=/var/lib/house/log

# Application build. --------------------------------------------

OBJS= housesaga.o \
      housesaga_event.o \
      housesaga_trace.o \
      housesaga_sensor.o \
      housesaga_metrics.o \
      housesaga_storage.o
LIBOJS=

all: housesaga

clean:
	rm -f *.o *.a housesaga

rebuild: clean all

%.o: %.c
	gcc -c -g -Os -o $@ $<

housesaga: $(OBJS)
	gcc -g -O -o housesaga $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

# Application installation. -------------------------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/saga
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/saga

install-runtime: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(STORE)
	if [ "x$(DESTDIR)" = "x" ] ; then chown -R house:house $(STORE) ; fi
	$(INSTALL) -m 0755 housesaga $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -T events.tcl $(DESTDIR)$(prefix)/bin/houseevents
	touch $(DESTDIR)/etc/default/housesaga

install-app: install-ui install-runtime

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/saga
	rm -f $(DESTDIR)$(prefix)/bin/housesaga

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/default/housesaga

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	install -m 0755 debian/postinst build/$(HAPP)/DEBIAN
	install -m 0755 debian/prerm build/$(HAPP)/DEBIAN
	install -m 0755 debian/postrm build/$(HAPP)/DEBIAN
	make DESTDIR=build/$(HAPP) install-package
	cd build/$(HAPP) ; find etc -type f | sed 's/etc/\/etc/' > DEBIAN/conffiles
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

# System installation. ------------------------------------------

include $(SHARE)/install.mak

