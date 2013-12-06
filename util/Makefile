SRCDIR = .
TOPDIR = $(SRCDIR)/..

include $(TOPDIR)/Make.defaults

all : $(patsubst %.in, %, $(wildcard *.in))

$(patsubst %.in,%,$(wildcard *.in)) :: % : %.in

clean :
	@rm -vf $(patsubst %.in, %, $(wildcard *.in))

install :
	$(INSTALL) -m 755 -d $(DESTDIR)$(LIBDIR)/kernel/install.d/
	for x in $(patsubst %.in,%,$(wildcard *.in)) ; do \
		$(INSTALL) -v -m 755 $$x $(DESTDIR)$(LIBDIR)/kernel/install.d/80-$${x}.install ; \
	done
	$(INSTALL) -m 755 -d $(DESTDIR)/usr/libexec/grubby/
	$(INSTALL) -m 755 functions $(DESTDIR)/usr/libexec/grubby/functions

.PHONY : all clean install

include $(TOPDIR)/Make.rules
