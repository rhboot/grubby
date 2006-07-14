include $(TOPDIR)/Makefile.tmpl

LIBS = bdevid
MODULES =
MODULES += scsi

bdevid_LIBS := -L. -ldl
bdevid_LIBS += $(shell pkg-config --libs glib-2.0)
bdevid_LIBS += -lnash -lblkid -luuid -ldevmapper -lselinux -lsepol
bdevid_OBJECTS = bdevid.o module.o probe.o sysfs.o

CFLAGS += $(shell pkg-config --cflags glib-2.0)
CFLAGS += "-DBDEVID_DEFAULT_SEARCH_PATH=\"/$(LIB)/bdevid/:/usr/$(LIB)/bdevid/\""

all :: $(OBJDIR)
all :: $(foreach lib,${LIBS},$(OBJDIR)/lib${lib}.so $(OBJDIR)/lib${lib}.a)
all :: $(foreach mod,${MODULES},$(OBJDIR)/${mod}.so)
all :: libbdevid.pc

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

libbdevid.pc: libbdevid.pc.in
	@sed "s/VERSIONSUBST/$(VERSION)/" < libbdevid.pc.in > libbdevid.pc
	@chmod 0644 libbdevid.pc

install : all
	@install -m 0755 -d $(DESTDIR)/usr/$(LIB)
	@cp -av $(OBJDIR)/libbdevid.{so*,a} $(DESTDIR)/usr/$(LIB)/
	@install -m 0755 -d $(DESTDIR)/usr/$(LIB)/pkgconfig
	@cp -av libbdevid.pc $(DESTDIR)/usr/$(LIB)/pkgconfig/
	@install -m 0755 -d $(DESTDIR)/$(LIB)/bdevid
	@cp -av $(foreach mod,$(MODULES),$(OBJDIR)/$(mod).so) \
		$(DESTDIR)/$(LIB)/bdevid/
	@for x in ${SUBDIRS} ; do \
		$(MAKE) -C $$x install ; \
	done

clean ::
	@rm -vf *.o libbdevid.pc
	@rm -rvf .objs

.PHONY : clean install
.SECONDARY : ${foreach lib,${LIBS},lib${lib}.so.${VERSION}}
