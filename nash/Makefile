CFLAGS=-O2 -Wall -DVERSION=\"$(VERSION)\"
VERSION=0.1

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))

ifeq (i386, $(ARCH))
MINILIBC=minilibc.o
CFLAGS+=-DUSE_MINILIBC=1 -DUSE_LOGDEV
LDFLAGS = -nostdlib /usr/lib/crt1.o
STATIC=-static
else
ifeq (sparc, $(ARCH))
MINILIBC=minilibc.o /usr/lib/libc.a
CFLAGS+=-DUSE_MINILIBC=1 -DUSE_LOGDEV
LDFLAGS = -nostdlib /usr/lib/crt1.o
STATIC=-static
else
CFLAGS+=-DUSE_MINILIBC=0
STATIC=-static
endif
endif

mandir=usr/share/man

nash: nash.o $(MINILIBC)
	$(CC) $(STATIC) -g $(LDFLAGS) -o $@ nash.o $(MINILIBC)

clean:
	rm -f nash $(MINILIBC) nash.o

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 -s nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
