TOPDIR ?= $(shell pwd)/../
export TOPDIR

TARGETS = nash
OBJECTS = lib.o dm.o block.o linkdetect.o nash.o network.o nfsmount.o \
	  nfsmount_clnt.o nfsmount_xdr.o dns.o hotplug.o

include ../Makefile.inc

LIBS=-ldevmapper -lparted -lblkid -lselinux -lsepol -luuid -lpopt \
	-lpump -lresolv

nash:: version.h $(OBJECTS)
	$(CC) $(LDFLAGS) -static -o $@ $^ $(LIBS)

hotplug: hotplug.c lib.o
	$(CC) $(CFLAGS) $(LDFLAGS) -DFWDEBUG -o $@ $^

debug:: hotplug
	$(MAKE) clean
debug:: $(shell pwd)/version.h
	$(MAKE) CFLAGS="$(CFLAGS) -DDEBUG" $(OBJECTS)
debug:: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean::
	rm -f debug hotplug

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
