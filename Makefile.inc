CVSTAG = r$(subst .,-,$(VERSION)-$(RELEASE))
CVSROOT = $(shell cat CVS/Root)

VERSION = $(shell awk -F= '/^VERSION=/ { print $$2 }' $(TOPDIR)/mkinitrd)
export VERSION
RELEASE = $(shell awk '/^Release:/ { print $$2 }' $(TOPDIR)/mkinitrd.spec.in)
export RELEASE

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))
ARCH := $(patsubst ppc%,ppc,$(ARCH))

CC = gcc
CFLAGS ?= -Wall -Werror -g -D_FORTIFY_SOURCE=2
LDFLAGS ?= $(CFLAGS) -Wl,--wrap,open,--wrap,fopen,--wrap,opendir,--wrap,socket \
	-Wl,--wrap,pipe

mandir = usr/share/man

all:: $(TARGETS) $(SUBDIRS)
	
.INTERMEDIATE:: $(shell pwd)/version.h
$(shell pwd)/version.h: $(TOPDIR)/mkinitrd
	@echo making $(shell pwd)/version.h
	@echo '#ifdef VERSION' > version.h
	@echo '#undef VERSION' >> version.h
	@echo '#endif' >> version.h
	@echo '#define' VERSION \"$(VERSION)\" >> version.h

$(TARGETS):: $(shell pwd)/version.h
	for n in $(SUBDIRS); do make -C $$n all ; done

.PHONY:: $(SUBDIRS)
$(SUBDIRS)::
	make -C $@ all

$(OBJECTS): %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean::
	rm -f $(TARGETS) *.o *.a version.h
	@for n in $(SUBDIRS); do make -C $$n clean ; done
