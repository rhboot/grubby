include $(TOPDIR)/Makefile.tmpl

SITELIB := $(shell python -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)[1:]")
CFLAGS += -I/usr/include/python2.4
LDFLAGS := -shared

all :: $(OBJDIR)/pybdevid.so

pybdevid_OBJECTS = pybdevid.o

$(OBJDIR)/pybdevid.so : $(pybdevid_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="pybdevid.so.$(VERSION)" -L $(OBJDIR) -o $@ -lbdevid -ldl $^
	
install : all
	@install -m 0755 -d $(DESTDIR)/$(SITELIB)
	@cp -av $(OBJDIR)/pybdevid.so $(DESTDIR)/$(SITELIB)/bdevid.so

clean ::
	@rm -vf $(pybdevid_OBJECTS) $(OBJDIR)/pybdevid.so

.PHONY : clean install
