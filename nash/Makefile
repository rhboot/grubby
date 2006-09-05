TOPDIR ?= $(shell pwd)/../
export TOPDIR

LIBS	= libnash.a
TARGETS = nash libnash.pc
OBJECTS = util.o linkdetect.o nash.o network.o nfsmount.o \
	  nfsmount_clnt.o nfsmount_xdr.o dns.o block_priv.o \
	  blkent.o

bdevid_OBJECTS = bdevid.o module.o probe.o sysfs.o
bdevid_OBJECTS = $(foreach obj,$(bdevid_OBJECTS),$(TOPDIR)/bdevid/${obj})

libnash_OBJECTS = lib.o hotplug.o wrap.o block.o uevent.o dm.o


include ../Makefile.inc

CFLAGS += -I$(TOPDIR)/nash/include

nash_LIBS = -L/$(LIB) -ldevmapper -lparted -lblkid -lselinux -lsepol -luuid
nash_LIBS += -lpopt -lresolv
nash_LIBS += $(shell pkg-config --libs libdhcp) -lm
CFLAGS += $(shell pkg-config --cflags libdhcp)

nash:: version.h $(OBJECTS) libnash.a
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $^ $(nash_LIBS)

nash.dynamic:: version.h $(OBJECTS) libnash.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(nash_LIBS)

libnash.a : $(libnash_OBJECTS) $(libbdevid_objects)
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
	rm -f debug hotplug *.o *.a libnash.pc

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/usr/$(LIB)
	mkdir -p $(BUILDROOT)/usr/$(LIB)/pkgconfig
	mkdir -p $(BUILDROOT)/usr/include/nash
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
	install -m 644 libnash.a $(BUILDROOT)/usr/$(LIB)
	install -m 644 libnash.pc $(BUILDROOT)/usr/$(LIB)/pkgconfig
	install -m 644 include/nash.h $(BUILDROOT)/usr/include
	install -m 644 include/nash/*.h $(BUILDROOT)/usr/include/nash
