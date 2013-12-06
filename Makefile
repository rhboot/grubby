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

VERSION=8.99

TOPDIR = $(shell echo $$PWD)

include $(TOPDIR)/Make.defaults

SUBDIRS = util

include $(TOPDIR)/Make.legacy

all clean install :: |
	@set -e ; for x in $(SUBDIRS) ; do \
		$(MAKE) -C $${x} TOPDIR=$(TOPDIR) SRCDIR=$(TOPDIR)/$@/ ARCH=$(ARCH) $@ ; \
	done

$(SUBDIRS) ::
	$(MAKE) -C $@ TOPDIR=$(TOPDIR) SRCDIR=$(TOPDIR)/$@/ ARCH=$(ARCH)

debug : clean
	$(MAKE) CFLAGS="${CFLAGS} -DDEBUG=1" all

test: all
	@export TOPDIR=$(TOPDIR)
	@./test.sh $(VERBOSE_TEST)

install :: all
	for x in $(SUBDIRS) ; do $(MAKE) -C $${x} TOPDIR=$(TOPDIR) SRCDIR=$(TOPDIR)/$@/ ARCH=$(ARCH) $@ ; done

clean ::
	@for x in $(SUBDIRS) ; do $(MAKE) -C $${x} TOPDIR=$(TOPDIR) SRCDIR=$(TOPDIR)/$@/ ARCH=$(ARCH) $@ ; done

.PHONY: $(SUBDIRS) clean install

include $(TOPDIR)/Make.rules

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

tag:
	git tag $(GITTAG) refs/heads/master

archive: tag
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
