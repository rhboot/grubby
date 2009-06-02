#
# Makefile
#
# Copyright 2007-2008 Red Hat, Inc.  All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

VERSION=6.0.86

TARGETS = grubby
OBJECTS = grubby.o

CFLAGS := $(CFLAGS) $(RPM_OPT_FLAGS) -DVERSION='"$(VERSION)"' -Wall -Werror
LDFLAGS := 

grubby_LIBS = -lnash -lbdevid
grubby_LIBS += -lparted -lblkid -luuid -lpopt -ldevmapper -lselinux -lsepol
grubby_LIBS += $(shell pkg-config --libs glib-2.0)

all: grubby

test: all
	@export TOPDIR=$(TOPDIR)
	@./test.sh

install: all
	mkdir -p $(DESTDIR)/sbin
	mkdir -p $(DESTDIR)/$(mandir)/man8
	install -m 755 new-kernel-pkg $(DESTDIR)/sbin
	if [ -f grubby ]; then \
		install -m 755 grubby $(DESTDIR)/sbin ; \
		install -m 644 grubby.8 $(DESTDIR)/$(mandir)/man8 ; \
	fi

grubby:: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(grubby_LIBS)

clean:
	rm -f *.o grubby *~
