TOPDIR ?= $(shell pwd)/../../
export TOPDIR

TARGETS = pybdevid.so

include $(TOPDIR)/Makefile.inc

SITELIB := $(shell python -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)[1:]")
CFLAGS += -I/usr/include/python2.4
CFLAGS += -I$(TOPDIR)/nash/include -I$(TOPDIR)/bdevid/include
LDFLAGS := -shared -L$(TOPDIR)/nash -L$(TOPDIR)/bdevid \

pybdevid_OBJECTS = pybdevid.o

pybdevid.so :: $(pybdevid_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="pybdevid.so.$(VERSION)" \
		-o $@ -lbdevid -ldl $^
	
install : all
	@install -m 0755 -d $(BUILDROOT)/$(SITELIB)
	@cp -av pybdevid.so $(BUILDROOT)/$(SITELIB)/bdevid.so

clean ::
	@rm -vf $(pybdevid_OBJECTS) pybdevid.so
