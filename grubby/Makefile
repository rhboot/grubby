VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' ../mkinitrd)

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))
ARCH := $(patsubst ppc%,ppc,$(ARCH))

CFLAGS = -Wall -g $(RPM_OPT_FLAGS) -DVERSION=\"$(VERSION)\"
LDFLAGS = -g

ifneq (,$(filter ppc64 x86_64 s390x,$(ARCH)))
LOADLIBES = /usr/lib64/libpopt.a
else
LOADLIBES = /usr/lib/libpopt.a
endif

all:	grubby

test:	all
	@./test.sh

install:    all
	mkdir -p $(BUILDROOT)/sbin
	mkdir -p $(BUILDROOT)/usr/share/man/man8
	install -m 755 new-kernel-pkg $(BUILDROOT)/sbin
	if [ -f grubby ]; then \
		install -m 755 -s grubby $(BUILDROOT)/sbin ; \
		install -m 644 grubby.8 $(BUILDROOT)/usr/share/man/man8 ; \
	fi

grubby:	grubby.o mount_by_label.o

clean:
	rm -f grubby
