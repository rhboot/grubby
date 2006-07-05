TOPDIR ?= $(shell pwd)/../
export TOPDIR

TARGETS = nash
OBJECTS = util.o dm.o block.o linkdetect.o nash.o network.o nfsmount.o \
	  nfsmount_clnt.o nfsmount_xdr.o dns.o

libnash_HEADERS = lib.h log.h hotplug.h
libnash_OBJECTS = lib.o hotplug.o wrap.o

include ../Makefile.inc

CFLAGS += -I$(TOPDIR)/nash/include

LIBS=-ldevmapper -lparted -lblkid -lselinux -lsepol -luuid -lpopt \
	-lpump -lresolv

nash:: version.h $(OBJECTS) libnash.a
	$(CC) $(LDFLAGS) -static -o $@ $^ $(LIBS)

libnash.a : $(libnash_OBJECTS)
	$(AR) crs $@ $^

hotplug: $(subst hotplug.o,,$(libnash_OBJECTS))
	$(CC) $(CFLAGS) $(LDFLAGS) -DFWDEBUG -o $@ hotplug.c $^

debug::
	$(MAKE) clean
debug:: $(shell pwd)/version.h hotplug libnash.a
	$(MAKE) CFLAGS="$(CFLAGS) -DDEBUG" $(OBJECTS)
debug:: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ libnash.a $(LIBS)

clean::
	rm -f debug hotplug *.o *.a

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8

install_devel:
	mkdir -p $(BUILDROOT)/usr/lib
	mkdir -p $(BUILDROOT)/usr/include/nash
	install -m 644 libnash.a $(BUILDROOT)/usr/lib
	install -m 644 nash.h $(BUILDROOT)/usr/include
	install -m 644 include/nash/$(LIBNASH_HEADERS) \
		$(BUILDROOT)/usr/include/nash
