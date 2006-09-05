CFLAGS=-Wall -DVERSION=\"$(VERSION)\" -g -D_FORTIFY_SOURCE=2
VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' ../mkinitrd)

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))

ifeq (i386, $(ARCH))
CC:=diet $(CC)
else
STATIC=-static
endif

LDFLAGS=-lnewt -lslang

mandir=usr/share/man

loadinitrd: loadinitrd.o dietstubs.o ctype.o
	$(CC) $(STATIC) -g -o $@ loadinitrd.o $(LDFLAGS) dietstubs.o ctype.o

clean:
	rm -f loadinitrd loadinitrd.o 

install:
	mkdir -p $(DESTDIR)/sbin
	install -m 755 -s loadinitrd $(DESTDIR)/sbin/loadinitrd
