VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' ../mkinitrd)

CFLAGS = -Wall -g $(RPM_OPT_FLAGS) -DVERSION=\"$(VERSION)\"
LDFLAGS = -g
LOADLIBES = /usr/lib/libpopt.a

all:	grubby

test:	all
	@./test.sh

install:    all
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/usr/share/man/man8
	install -m 755 -s grubby $(BUILDROOT)/sbin
	install -m 755 new-kernel-pkg $(BUILDROOT)/sbin
	install -m 644 grubby.8 $(BUILDROOT)/usr/share/man/man8

grubby:	grubby.o mount_by_label.o

clean:
	rm -f grubby
