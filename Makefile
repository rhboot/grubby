VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' mkinitrd)
RELEASE=$(shell awk '/^Release:/ { print $$2 }' mkinitrd.spec.in)
CVSTAG = r$(subst .,-,$(VERSION)-$(RELEASE))

ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))

SUBDIRS = nash grubby

#ifeq ($(ARCH),sparc)
#SUBDIRS += loadinitrd
#endif

#ifeq ($(ARCH),i386)
#SUBDIRS += loadinitrd
#endif

mandir=usr/share/man

all:
	for n in $(SUBDIRS); do make -C $$n; done

test:	all
	cd grubby; make test

install:
	for n in $(SUBDIRS); do make -C $$n install BUILDROOT=$(BUILDROOT); done
	for i in sbin $(mandir)/man8; do \
		if [ ! -d $(BUILDROOT)/$$i ]; then \
			mkdir -p $(BUILDROOT)/$$i; \
		fi; \
	done
	sed 's/%VERSIONTAG%/$(VERSION)/' < mkinitrd > $(BUILDROOT)/sbin/mkinitrd
	install -m755 installkernel $(BUILDROOT)/sbin/installkernel
	chmod 755 $(BUILDROOT)/sbin/mkinitrd
	install -m644 mkinitrd.8 $(BUILDROOT)/$(mandir)/man8/mkinitrd.8

clean:
	for n in $(SUBDIRS); do make -C $$n clean; done

archive:
	# cvs tag -F $(CVSTAG) .
	@rm -rf /tmp/mkinitrd-$(VERSION)
	cd /tmp; cvs -Q -d $(CVSROOT) export -r$(CVSTAG) mkinitrd || :
	cd /tmp/mkinitrd; sed "s/VERSIONSUBST/$(VERSION)/" < mkinitrd.spec.in > mkinitrd.spec
	mv /tmp/mkinitrd /tmp/mkinitrd-$(VERSION)
	dir=$$PWD; cd /tmp; tar -cv --bzip2 -f $$dir/mkinitrd-$(VERSION).tar.bz2 mkinitrd-$(VERSION)
	rm -rf /tmp/mkinitrd-$(VERSION)
	@echo "The archive is in mkinitrd-$(VERSION).tar.bz2"
