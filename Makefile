VERSION=$(shell awk '/define version/ { print $$3 }' mkinitrd.spec)
CVSTAG = r$(subst .,-,$(VERSION))

install:
	for i in sbin usr/man/man8; do \
		if [ ! -d $(BUILDROOT)/$$i ]; then \
			mkdir -p $(BUILDROOT)/$$i; \
		fi; \
	done
	sed 's/%VERSIONTAG%/$(VERSION)/' < mkinitrd > $(BUILDROOT)/sbin/mkinitrd
	chmod 755 $(BUILDROOT)/sbin/mkinitrd
	install -m644 mkinitrd.8 $(BUILDROOT)/usr/man/man8/mkinitrd.8

archive:
	cvs tag -F $(CVSTAG) .
	@rm -rf /tmp/mkinitrd-$(VERSION)
	@cd /tmp; cvs -Q -d $(CVSROOT) export -r$(CVSTAG) mkinitrd || :
	@mv /tmp/mkinitrd /tmp/mkinitrd-$(VERSION)
	@dir=$$PWD; cd /tmp; tar cvzf $$dir/mkinitrd-$(VERSION).tar.gz mkinitrd-$(VERSION)
	@rm -rf /tmp/mkinitrd-$(VERSION)
	@echo "The archive is in mkinitrd-$(VERSION).tar.gz"
