TOPDIR ?= $(shell pwd)/../../
export TOPDIR

TARGETS = pybdevid.so

include $(TOPDIR)/Makefile.inc

SITELIB := $(shell python -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)[1:]")
PYVER := $(shell python -c "import sys; print sys.version[0:3]")
CFLAGS += -I/usr/include/python$(PYVER)
CFLAGS += -I$(TOPDIR)/nash/include -I$(TOPDIR)/bdevid/include
CFLAGS += $(shell pkg-config --cflags glib-2.0)
LDFLAGS = -shared -L$(TOPDIR)/nash -L$(TOPDIR)/bdevid
LDFLAGS += -Wl,-soname="pybdevid.so.$(VERSION)",--as-needed
LDFLAGS += -lpython$(PYVER) -ldl $(shell pkg-config --libs glib-2.0)

pybdevid_OBJECTS = pybdevid.o
pybdevid_LIBS = -lbdevid -lnash -ldevmapper -lparted -lblkid

pybdevid.so :: $(pybdevid_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lbdevid $(pybdevid_LIBS)

install : all
	@install -m 0755 -d $(DESTDIR)/$(SITELIB)
	@cp -av pybdevid.so $(DESTDIR)/$(SITELIB)/bdevid.so

clean ::
	@rm -vf $(pybdevid_OBJECTS) pybdevid.so
