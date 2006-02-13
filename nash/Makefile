CFLAGS=-Wall -Werror -DVERSION=\"$(VERSION)\" -g -D_FORTIFY_SOURCE=2

VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' ../mkinitrd)

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))

LIBS=-ldevmapper -lselinux -lsepol -lparted -lblkid -luuid -lpopt \
	-lpump -lresolv

LDFLAGS=$(CFLAGS)

mandir=usr/share/man

OBJECTS=lib.o dm.o block.o linkdetect.o nash.o network.o nfsmount.o \
	nfsmount_clnt.o nfsmount_xdr.o dns.o hotplug.o

nash: $(OBJECTS)
	$(CC) $(LDFLAGS) -static -o $@ $^ $(LIBS)

hotplug: hotplug.c lib.o
	$(CC) $(CFLAGS) $(LDFLAGS) -DFWDEBUG -o $@ $^

debug:: hotplug
debug:: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f hotplug nash debug *.o *.a

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 -s nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
