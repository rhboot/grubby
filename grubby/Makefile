TOPDIR ?= $(shell pwd)/../
export TOPDIR

TARGETS = grubby
OBJECTS = grubby.o

include ../Makefile.inc

CFLAGS := $(CFLAGS) -iquote../nash/ -I../nash/include/ $(RPM_OPT_FLAGS)
LDFLAGS := -Wl,--wrap,open,--wrap,fopen,--wrap,opendir,--wrap,socket \
	-Wl,--wrap,pipe

grubby_LIBS = -lparted -lblkid -luuid -lpopt -ldevmapper -lselinux -lsepol

test: all
	@./test.sh

install: all
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 new-kernel-pkg $(BUILDROOT)/sbin
	if [ -f grubby ]; then \
		install -m 755 grubby $(BUILDROOT)/sbin ; \
		install -m 644 grubby.8 $(BUILDROOT)/$(mandir)/man8 ; \
	fi

grubby:: $(OBJECTS) ../nash/libnash.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ \
		-Wl,-Bstatic $(grubby_LIBS) -Wl,-Bdynamic

