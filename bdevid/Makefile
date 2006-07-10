include $(TOPDIR)/Makefile.tmpl

LIBS = bdevid
MODULES = test
MODULES += scsi

bdevid_LIBS := -ldl
bdevid_LIBS += `pkg-config --libs glib-2.0`
bdevid_OBJECTS = bdevid.o module.o

all :: $(OBJDIR)
all :: $(foreach lib,${LIBS},$(OBJDIR)/lib${lib}.so $(OBJDIR)/lib${lib}.a)
all :: $(foreach mod,${MODULES},$(OBJDIR)/${mod}.so)

$(foreach lib,$(MODULES),$(OBJDIR)/$(lib).so) : $(OBJDIR)/%.so : %.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="$*.so.$(VERSION)" -L$(OBJDIR) -o $@ -lbdevid $($*_LIBS) $^

$(OBJDIR)/libbdevid.so.$(VERSION) : $(bdevid_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="$@" -L$(OBJDIR) -o $@ $(bdevid_LIBS) $^

$(OBJDIR)/%.so : $(OBJDIR)/%.so.$(VERSION)
	@ln -vsf $(shell basename $<) $@

# XXX doesn't work, not quite sure why...
#$(foreach lib,$(LIBS),$(OBJDIR)/$(lib).a) : $(OBJDIR)/%.a : $(OBJDIR)/%.a($(%_OBJECTS))
#	ranlib $@
$(OBJDIR)/libbdevid.a : $(OBJDIR)/libbdevid.a($(bdevid_OBJECTS))
	ranlib $@

install : all
	@for x in ${SUBDIRS} ; do \
		$(MAKE) -C $$x install ; \
	done
	@install -m 0755 -d $(DESTDIR)/lib
	@cp -av $(OBJDIR)/libbdevid.{so*} $(DESTDIR)/lib/
	@install -m 0755 -d $(DESTDIR)/usr/lib/pkgconfig
	@cp -av $(OBJDIR)/libbdevid.a $(DESTDIR)/usr/lib/
	@cp -av libbdevid.pc $(DESTDIR)/usr/lib/pkgconfig/

clean ::
	@rm -vf *.o
	@rm -rvf .objs

.PHONY : clean install
.SECONDARY : ${foreach lib,${LIBS},lib${lib}.so.${VERSION}}
