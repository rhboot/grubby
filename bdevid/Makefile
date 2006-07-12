include $(TOPDIR)/Makefile.tmpl

LIBS = bdevid
MODULES =
#MODULES += test
MODULES += scsi

bdevid_LIBS := -ldl
bdevid_LIBS += $(shell pkg-config --libs glib-2.0 libnash)
bdevid_OBJECTS = bdevid.o module.o probe.o sysfs.o

CFLAGS += $(shell pkg-config --cflags glib-2.0 libnash)

all :: $(OBJDIR)
all :: $(foreach lib,${LIBS},$(OBJDIR)/lib${lib}.so $(OBJDIR)/lib${lib}.a)
all :: $(foreach mod,${MODULES},$(OBJDIR)/${mod}.so)

$(foreach lib,$(MODULES),$(OBJDIR)/$(lib).so) : $(OBJDIR)/%.so : %.o
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -Wl,-soname="$*.so.$(VERSION)" -L$(OBJDIR) -o $@ $< $($*_LIBS) -lbdevid $(bdevid_LIBS)

$(OBJDIR)/libbdevid.so.$(VERSION) : $(bdevid_OBJECTS)
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -Wl,-soname="$(shell basename $@)" -o $@ $^ $(bdevid_LIBS)

$(OBJDIR)/%.so : $(OBJDIR)/%.so.$(VERSION)
	@ln -vsf $(shell basename $<) $@

# XXX doesn't work, not quite sure why...
#$(foreach lib,$(LIBS),$(OBJDIR)/$(lib).a) : $(OBJDIR)/%.a : $(OBJDIR)/%.a($(%_OBJECTS))
#	ranlib $@
$(OBJDIR)/libbdevid.a : $(OBJDIR)/libbdevid.a($(bdevid_OBJECTS))
	ranlib $@

install : all
	@install -m 0755 -d $(DESTDIR)/usr/lib
	@cp -av $(OBJDIR)/libbdevid.{so*,a} $(DESTDIR)/usr/lib/
	@install -m 0755 -d $(DESTDIR)/usr/lib/pkgconfig
	@cp -av libbdevid.pc $(DESTDIR)/usr/lib/pkgconfig/
	@install -m 0755 -d $(DESTDIR)/lib/bdevid
	@cp -av $(foreach mod,$(MODULES),$(OBJDIR)/$(mod).so) \
		$(DESTDIR)/lib/bdevid/
	@for x in ${SUBDIRS} ; do \
		$(MAKE) -C $$x install ; \
	done

clean ::
	@rm -vf *.o
	@rm -rvf .objs

.PHONY : clean install
.SECONDARY : ${foreach lib,${LIBS},lib${lib}.so.${VERSION}}
