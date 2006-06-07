include $(TOPDIR)/Makefile.tmpl

LIBS = bdevid
MODULES = scsi

bdevid_LIBS = -ldl -lmodloader
bdevid_OBJECTS = bdevid.o

scsi_LIBS = -lbdevid -lsysfs
scsi_OBJECTS = scsi.o

all :: $(OBJDIR)
all :: $(foreach lib,${LIBS},$(OBJDIR)/lib${lib}.so $(OBJDIR)/lib${lib}.a)
all :: $(foreach mod,${MODULES},$(OBJDIR)/${mod}.so)

$(foreach lib,$(MODULES),$(OBJDIR)/$(lib).so) : $(OBJDIR)/%.so : %.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="$*.so.$(VERSION)" -L$(OBJDIR) -o $@ $($*_LIBS) $^

$(foreach lib,$(LIBS),$(OBJDIR)/lib$(lib).so.$(VERSION)) : $(OBJDIR)/lib%.so.$(VERSION) : %.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="lib$*.so.$(VERSION)" -L$(OBJDIR) -o $@ $($*_LIBS) $^

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
