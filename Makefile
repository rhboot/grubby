
TOPDIR = $(shell pwd)
export TOPDIR

SUBDIRS = nash bdevid grubby

include Makefile.inc

test: all
	cd grubby; make test

install:
	for n in $(SUBDIRS); do make -C $$n install DESTDIR=$(DESTDIR); done
	for i in sbin $(mandir)/man8; do \
		if [ ! -d $(DESTDIR)/$$i ]; then \
			mkdir -p $(DESTDIR)/$$i; \
		fi; \
	done
	sed 's/%VERSIONTAG%/$(VERSION)/' < mkinitrd > $(DESTDIR)/sbin/mkinitrd
	install -m755 installkernel $(DESTDIR)/sbin/installkernel
	chmod 755 $(DESTDIR)/sbin/mkinitrd
	install -m644 mkinitrd.8 $(DESTDIR)/$(mandir)/man8/mkinitrd.8

archive:
	cvs tag -F $(CVSTAG) .
	@rm -rf /tmp/mkinitrd-$(VERSION)
	@cd /tmp; cvs -Q -d $(CVSROOT) export -r$(CVSTAG) mkinitrd || :
	@cd /tmp/mkinitrd; sed "s/VERSIONSUBST/$(VERSION)/" < mkinitrd.spec.in > mkinitrd.spec
	@mv /tmp/mkinitrd /tmp/mkinitrd-$(VERSION)
	@dir=$$PWD; cd /tmp; tar -cv --bzip2 -f $$dir/mkinitrd-$(VERSION).tar.bz2 mkinitrd-$(VERSION)
	@rm -rf /tmp/mkinitrd-$(VERSION)
	@echo "The archive is in mkinitrd-$(VERSION).tar.bz2"
