VERSION=$(shell awk -F= '/^VERSION=/ { print $$2 }' ../mkinitrd)

CFLAGS = -Wall -g $(RPM_OPT_FLAGS) -DVERSION=\"$(VERSION)\"
LDFLAGS = -g
LOADLIBES = /usr/lib/libpopt.a

all:	grubby

test:	all
	@echo "Parse/write comparison..."
	@for n in test/grub.[0-9]*; do \
		./grubby --remove-kernel 1234 -c $$n -o - | cmp $$n; \
	done

install:    all
	mkdir -p $(BUILDROOT)/sbin
	install -m 755 -s grubby $(BUILDROOT)/sbin
	install -m 755 new-kernel-pkg $(BUILDROOT)/sbin

clean:
	rm -f grubby
