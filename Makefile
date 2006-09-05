TOPDIR ?= $(shell pwd)/../
export TOPDIR

TARGETS = grubby
OBJECTS = grubby.o

include ../Makefile.inc

CFLAGS := $(CFLAGS) -iquote../nash/ \
	-I$(TOPDIR)/nash/include -I$(TOPDIR)/bdevid/include $(RPM_OPT_FLAGS)
LDFLAGS := -L$(TOPDIR)/nash -L$(TOPDIR)/bdevid \
	-Wl,--wrap,open,--wrap,fopen,--wrap,opendir,--wrap,socket \
	-Wl,--wrap,pipe

grubby_LIBS = -lnash
grubby_LIBS += -lparted -lblkid -luuid -lpopt -ldevmapper -lselinux -lsepol
grubby_LIBS += $(shell pkg-config --libs libdhcp glib-2.0)

test: all
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
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ \
		-Wl,-Bstatic $(grubby_LIBS) -Wl,-Bdynamic

