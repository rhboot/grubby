#
# Makefile
#
# Copyright 2007-2009 Red Hat, Inc.  All rights reserved.
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

VERSION=8.40

TARGETS = grubby
OBJECTS = grubby.o log.o

CC = gcc
RPM_OPT_FLAGS ?= -O2 -g -pipe -Wp,-D_FORTIFY_SOURCE=2 -fstack-protector
CFLAGS += $(RPM_OPT_FLAGS) -std=gnu99 -Wall -Werror -Wno-error=unused-function -Wno-unused-function -ggdb
LDFLAGS := 
VERBOSE_TEST :=
ifneq ($(VERBOSE_TEST),)
	VERBOSE_TEST="--verbose"
endif

grubby_LIBS = -lblkid -lpopt

all: grubby

debug : clean
	$(MAKE) CFLAGS="${CFLAGS} -DDEBUG=1" all

%.o : %.c
	$(CC) $(CFLAGS) -DVERSION='"$(VERSION)"' -c -o $@ $<

test: all
	@export TOPDIR=$(TOPDIR)
	@./test.sh $(VERBOSE_TEST)
	@./test-bls.sh $(VERBOSE_TEST)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/sbin
	mkdir -p $(DESTDIR)/$(mandir)/man8
	install -m 755 new-kernel-pkg $(DESTDIR)$(PREFIX)/sbin
	install -m 644 new-kernel-pkg.8 $(DESTDIR)/$(mandir)/man8
	install -m 755 installkernel $(DESTDIR)$(PREFIX)/sbin
	install -m 644 installkernel.8 $(DESTDIR)/$(mandir)/man8
	install -m 755 grubby-bls $(DESTDIR)$(PREFIX)/sbin
	if [ -f grubby ]; then \
		install -m 755 grubby $(DESTDIR)$(PREFIX)/sbin ; \
		install -m 644 grubby.8 $(DESTDIR)/$(mandir)/man8 ; \
	fi

grubby:: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(grubby_LIBS)

clean:
	rm -f *.o grubby *~

GITTAG = $(VERSION)-1

test-archive:
	@rm -rf /tmp/grubby-$(VERSION) /tmp/grubby-$(VERSION)-tmp
	@mkdir -p /tmp/grubby-$(VERSION)-tmp
	@git archive --format=tar $(shell git branch | awk '/^*/ { print $$2 }') | ( cd /tmp/grubby-$(VERSION)-tmp/ ; tar x )
	@git diff | ( cd /tmp/grubby-$(VERSION)-tmp/ ; patch -s -p1 -b -z .gitdiff )
	@mv /tmp/grubby-$(VERSION)-tmp/ /tmp/grubby-$(VERSION)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/grubby-$(VERSION).tar.bz2 grubby-$(VERSION)
	@rm -rf /tmp/grubby-$(VERSION)
	@echo "The archive is in grubby-$(VERSION).tar.bz2"

archive:
	git tag $(GITTAG) refs/heads/master
	@rm -rf /tmp/grubby-$(VERSION) /tmp/grubby-$(VERSION)-tmp
	@mkdir -p /tmp/grubby-$(VERSION)-tmp
	@git archive --format=tar $(GITTAG) | ( cd /tmp/grubby-$(VERSION)-tmp/ ; tar x )
	@mv /tmp/grubby-$(VERSION)-tmp/ /tmp/grubby-$(VERSION)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/grubby-$(VERSION).tar.bz2 grubby-$(VERSION)
	@rm -rf /tmp/grubby-$(VERSION)
	@echo "The archive is in grubby-$(VERSION).tar.bz2"

upload: archive
	@scp grubby-$(VERSION).tar.bz2 fedorahosted.org:grubby

ci: test
