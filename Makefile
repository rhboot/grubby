install:
	for i in sbin usr/man/man8; do \
		if [ ! -d $(BUILDROOT)/$$i ]; then \
			mkdir -p $(BUILDROOT)/$$i; \
		fi; \
	done
	install -m755 mkinitrd $(BUILDROOT)/sbin/mkinitrd
	install -m644 mkinitrd.8 $(BUILDROOT)/usr/man/man8/mkinitrd.8
