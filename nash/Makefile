TOPDIR ?= $(shell pwd)/../
export TOPDIR

TARGETS = nash libnash.pc
OBJECTS = util.o dm.o linkdetect.o nash.o network.o nfsmount.o \
	  nfsmount_clnt.o nfsmount_xdr.o dns.o block_priv.o

libnash_OBJECTS = lib.o hotplug.o wrap.o block.o

include ../Makefile.inc

CFLAGS += -I$(TOPDIR)/nash/include

LIBS	= -L/lib -ldevmapper -lparted -lblkid -lselinux -lsepol -luuid
LIBS	+= -lpopt -lresolv
LIBS   += $(shell pkg-config --libs libdhcp) -lm
CFLAGS += $(shell pkg-config --cflags libdhcp)

nash:: version.h $(OBJECTS) libnash.a
	$(CC) $(LDFLAGS) -static -o $@ $^ $(LIBS)

libnash.a : $(libnash_OBJECTS)
	$(AR) crs $@ $^

libnash.pc:: libnash.pc.in
	@sed "s/VERSIONSUBST/$(VERSION)/" < libnash.pc.in > libnash.pc

hotplug: $(subst hotplug.o,,$(libnash_OBJECTS))
	$(CC) $(CFLAGS) $(LDFLAGS) -DFWDEBUG -o $@ hotplug.c $^ -lblkid

debug::
	$(MAKE) clean
debug:: $(shell pwd)/version.h hotplug libnash.a
	$(MAKE) CFLAGS="$(CFLAGS) -DDEBUG" $(OBJECTS)
debug:: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ libnash.a $(LIBS)

clean::
	rm -f debug hotplug *.o *.a libnash.pc

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/usr/lib
	mkdir -p $(BUILDROOT)/usr/lib/pkgconfig
	mkdir -p $(BUILDROOT)/usr/include/nash
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
	install -m 644 libnash.a $(BUILDROOT)/usr/lib
	install -m 644 libnash.pc $(BUILDROOT)/usr/lib/pkgconfig
	install -m 644 include/nash.h $(BUILDROOT)/usr/include
	install -m 644 include/nash/*.h $(BUILDROOT)/usr/include/nash
