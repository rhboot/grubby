TOPDIR ?= $(shell pwd)/../
export TOPDIR

LIBS	= libnash.a
TARGETS = nash libnash.pc
OBJECTS = util.o linkdetect.o nash.o network.o nfsmount.o \
	  nfsmount_clnt.o nfsmount_xdr.o dns.o block_priv.o \
	  blkent.o waitdev.o bdevid.o

libnash_OBJECTS = lib.o hotplug.o wrap.o block.o uevent.o dm.o


include ../Makefile.inc

CFLAGS += -I$(TOPDIR)/nash/include -I$(TOPDIR)/bdevid/include
CFLAGS += $(shell pkg-config --cflags libdhcp glib-2.0)

nash_LIBS = -L$(TOPDIR)/nash -L$(TOPDIR)/bdevid -L/$(LIB)
nash_LIBS += -ldevmapper -lparted -lblkid -lselinux -lsepol
nash_LIBS += -luuid -lpopt -lresolv -lbdevid -ldl
nash_LIBS += $(shell pkg-config --libs libdhcp glib-2.0) -lm

nash:: version.h $(OBJECTS) libnash.a
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $^ $(nash_LIBS)

nash.dynamic:: version.h $(OBJECTS) libnash.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(nash_LIBS)

libnash.a :: $(libnash_OBJECTS)
	$(AR) crs $@ $^

libnash.pc:: libnash.pc.in
	@sed -e "s/VERSIONSUBST/$(VERSION)/" \
	     -e "s/LIBSUBST/$(LIB)/" < libnash.pc.in > libnash.pc

hotplug: $(subst hotplug.o,,$(libnash_OBJECTS))
	$(CC) $(CFLAGS) $(LDFLAGS) -DFWDEBUG -o $@ hotplug.c $^ -lblkid

debug::
	$(MAKE) clean
debug:: $(shell pwd)/version.h hotplug libnash.a
	$(MAKE) CFLAGS="$(CFLAGS) -DDEBUG" $(OBJECTS)
debug:: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ libnash.a $(nash_LIBS)

clean::
	rm -f nash nash.dynamic debug hotplug *.o *.a libnash.pc

install:
	mkdir -p $(DESTDIR)/sbin
	mkdir -p $(DESTDIR)/usr/$(LIB)
	mkdir -p $(DESTDIR)/usr/$(LIB)/pkgconfig
	mkdir -p $(DESTDIR)/usr/include/nash
	mkdir -p $(DESTDIR)/$(mandir)/man8
	install -m 755 nash $(DESTDIR)/sbin
	install -m 644 nash.8 $(DESTDIR)/$(mandir)/man8
	install -m 644 libnash.a $(DESTDIR)/usr/$(LIB)
	install -m 644 libnash.pc $(DESTDIR)/usr/$(LIB)/pkgconfig
	install -m 644 include/*.h $(DESTDIR)/usr/include
	install -m 644 include/nash/*.h $(DESTDIR)/usr/include/nash
