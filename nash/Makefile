CFLAGS=-Wall -DVERSION=\"$(VERSION)\" -g
VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' ../mkinitrd)

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))

ifeq (i386, $(ARCH))
CC:=diet $(CC)
CFLAGS += -DUSE_DIET=1
else
STATIC=-static
endif

mandir=usr/share/man

nash: nash.o mount_by_label.o name_to_dev_t.o
	$(CC) $(STATIC) -g $(LDFLAGS) -o $@ $^

clean:
	rm -f nash $(MINILIBC) *.o

install:
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/$(mandir)/man8
	install -m 755 -s nash $(BUILDROOT)/sbin
	install -m 644 nash.8 $(BUILDROOT)/$(mandir)/man8
