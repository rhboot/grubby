VERSION=$(shell awk '/Version:/ { print $$2 }' mkinitrd.spec)
CVSTAG = r$(subst .,-,$(VERSION))

mandir=usr/share/man

install:
	for i in sbin $(mandir)/man8; do \
		if [ ! -d $(BUILDROOT)/$$i ]; then \
			mkdir -p $(BUILDROOT)/$$i; \
		fi; \
	done
	sed 's/%VERSIONTAG%/$(VERSION)/' < mkinitrd > $(BUILDROOT)/sbin/mkinitrd
	chmod 755 $(BUILDROOT)/sbin/mkinitrd
	install -m644 mkinitrd.8 $(BUILDROOT)/$(mandir)/man8/mkinitrd.8

archive:
	cvs tag -F $(CVSTAG) .
	@rm -rf /tmp/mkinitrd-$(VERSION)
	@cd /tmp; cvs -Q -d $(CVSROOT) export -r$(CVSTAG) mkinitrd || :
	@mv /tmp/mkinitrd /tmp/mkinitrd-$(VERSION)
	@dir=$$PWD; cd /tmp; tar cvjf $$dir/mkinitrd-$(VERSION).tar.bz2 mkinitrd-$(VERSION)
	@rm -rf /tmp/mkinitrd-$(VERSION)
	@echo "The archive is in mkinitrd-$(VERSION).tar.bz2"
