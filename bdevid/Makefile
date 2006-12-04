TOPDIR ?= $(shell pwd)/../
export TOPDIR

bdevid_OBJECTS = bdevid.o module.o probe.o sysfs.o probelib.o
bdevid_LIBS := -L. -L$(TOPDIR)/nash -ldl
bdevid_LIBS += $(shell pkg-config --libs glib-2.0)
bdevid_LIBS += -lnash -lparted -lblkid -luuid -ldevmapper -lselinux -lsepol

bdevidprobe_OBJECTS = probelib.o

command_LIBS := -L. -L$(TOPDIR)/nash -lbdevid -ldl
command_LIBS += $(shell pkg-config --libs glib-2.0) -lbdevid
command_LIBS += -lnash -lparted -lblkid -luuid -ldevmapper -lselinux -lsepol

MODULES = scsi ata usb
OBJECTS = $(bdevid_OBJECTS)
LIBS = libbdevid.so
TARGETS = libbdevid.pc libbdevidprobe.pc bdevid
TARGETS = libbdevidprobe.a
TARGETS += $(foreach dso, $(MODULES), ${dso}.so)
SUBDIRS = python

include ../Makefile.inc

CFLAGS += -I$(TOPDIR)/nash/include -I$(TOPDIR)/bdevid/include
CFLAGS += $(shell pkg-config --cflags glib-2.0)
CFLAGS += "-DBDEVID_DEFAULT_SEARCH_PATH=\"/$(LIB)/bdevid/:/usr/$(LIB)/bdevid/\""

LDFLAGS := -L$(TOPDIR)/nash -L$(TOPDIR)/bdevid

bdevid :: command.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(command_LIBS)

$(foreach lib,$(MODULES),$(lib).so) :: %.so : %.o libbdevidprobe.a
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="$*.so.$(VERSION)" -o $@ $< -shared $($*_LIBS) -lbdevidprobe

libbdevid.so.$(VERSION) : $(bdevid_OBJECTS)
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -Wl,-soname="$(shell basename $@)" -o $@ $^ $(bdevid_LIBS)

libbdevidprobe.a : $(bdevidprobe_OBJECTS)
	$(AR) crs $@ $^
	ranlib $@

%.so : %.so.$(VERSION)
	@ln -vsf $(shell basename $<) $@

%.pc:: %.pc.in
	@sed -e "s/VERSIONSUBST/$(VERSION)/" \
	     -e "s/LIBSUBST/$(LIB)/" < $< > $@

clean::
	rm -f *.o *.a *.so.* *.so *.pc bdevid

install: all
	mkdir -p $(DESTDIR)/sbin
	mkdir -p $(DESTDIR)/$(LIB)/bdevid
	mkdir -p $(DESTDIR)/usr/$(LIB)
	mkdir -p $(DESTDIR)/usr/$(LIB)/pkgconfig
	mkdir -p $(DESTDIR)/usr/include/bdevid
	install -m 755 bdevid $(DESTDIR)/sbin
	install -m 644 libbdevidprobe.a $(DESTDIR)/usr/$(LIB)
	install -m 644 libbdevid.so.$(VERSION) $(DESTDIR)/usr/$(LIB)
	ln -sf libbdevid.so.$(VERSION) $(DESTDIR)/usr/$(LIB)/libbdevid.so
	install -m 644 libbdevidprobe.pc $(DESTDIR)/usr/$(LIB)/pkgconfig
	install -m 644 libbdevid.pc $(DESTDIR)/usr/$(LIB)/pkgconfig
	install -m 644 include/bdevid.h $(DESTDIR)/usr/include
	install -m 644 include/bdevid/*.h $(DESTDIR)/usr/include/bdevid
	cp -av $(foreach mod,$(MODULES),$(mod).so) $(DESTDIR)/$(LIB)/bdevid/
	for x in ${SUBDIRS} ; do \
		$(MAKE) -C $$x install ; \
	done
