
TARGETS = nash
OBJECTS = lib.o dm.o block.o linkdetect.o nash.o network.o nfsmount.o \
	  nfsmount_clnt.o nfsmount_xdr.o dns.o hotplug.o

include ../Makefile.inc

LIBS=-ldevmapper -lselinux -lsepol -lparted -lblkid -luuid -lpopt \
	-lpump -lresolv

nash::
	echo $(OBJECTS)
nash:: $(OBJECTS)
	$(CC) $(LDFLAGS) -static -o $@ $^ $(LIBS)

hotplug: hotplug.c lib.o
	$(CC) $(CFLAGS) $(LDFLAGS) -DFWDEBUG -o $@ $^

debug:: hotplug
debug::
	make clean
	make CFLAGS="$(CFLAGS) -DDEBUG" $(OBJECTS) 
debug:: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean::
	rm -f debug hotplug

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 -s nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
