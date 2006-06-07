include $(TOPDIR)/Makefile.tmpl

LIBS = bdevid
MODULES = scsi

bdevid_LIBS = -ldl -lmodloader
bdevid_OBJECTS = bdevid.o

scsi_LIBS = -lbdevid
scsi_OBJECTS = scsi.o

all :: $(OBJDIR)
all :: $(foreach lib,${LIBS},$(OBJDIR)/lib${lib}.so $(OBJDIR)/lib${lib}.a)
all :: $(foreach mod,${MODULES},$(OBJDIR)/${mod}.so)

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(foreach lib,$(MODULES),$(OBJDIR)/$(lib).so) : $(OBJDIR)/%.so : %.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="$*.so.$(VERSION)" -L$(OBJDIR) -o $@ $($*_LIBS) $^

$(foreach lib,$(LIBS),$(OBJDIR)/lib$(lib).so.$(VERSION)) : $(OBJDIR)/lib%.so.$(VERSION) : %.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname="lib$*.so.$(VERSION)" -L$(OBJDIR) -o $@ $($*_LIBS) $^

$(OBJDIR)/%.so : $(OBJDIR)/%.so.$(VERSION)
	@echo ln -sf $(shell basename $<) $@
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



#pyblock-$(VERSION).tar.bz2 : 
#	@rm -rvf /tmp/$@
#	@rm -vf $(PWD)/../pyblock-$(VERSION)
#	ln -sf $(PWD) $(PWD)/../pyblock-$(VERSION)
#	tar cjhf /tmp/$@ --exclude playpen $(PWD)/../pyblock-$(VERSION)
#	mv /tmp/$@ $(PWD)/
#
#.PHONY : pyblock-$(VERSION).tar.bz2
#archive : clean pyblock-$(VERSION).tar.bz2
#
#flat_install: all
#	@install -v -m 0755 -d ${DESTDIR}/
#	@for x in $(PYFILES) ; do \
#		install -v -m 0755 $$x ${DESTDIR}/$$x ; \
#	done
#	@for x in $(LIBS) ; do \
#		install -v -m 0755 $$x.$(VERSION) ${DESTDIR}/$$x ; \
#	done
#
#install_debug: clean all
#	install -m 0755 -d "${DESTDIR}/RHupdates/block"
#	install -m 0755 -d "${DESTDIR}/RHupdates/${DEBUGLIB}/${SITELIB}/block"
#	install -m 0755 -d "${DESTDIR}/RHupdates/usr/src/debug/pyblock-${VERSION}"
#	@for x in ${PYFILES} ; do \
#		install -m 0755 $$x ${DESTDIR}/RHupdates/block/$$x ; \
#	done
#	@for x in "*.[ch]" ; do \
#		install -m 0644 $$x \
#		  "${DESTDIR}/RHupdates/usr/src/debug/pyblock-${VERSION}/" ; \
#	done
#	@for x in ${LIBS} ; do \
#		echo /usr/lib/rpm/debugedit -b "${PWD}" \
#			-d /usr/src/debug \
#			"$$x.${VERSION}" ; \
#		/usr/lib/rpm/debugedit -b "${PWD}" \
#			-d /usr/src/debug \
#			"$$x.${VERSION}" ; \
#		echo eu-strip -f \
#		  "${DESTDIR}/RHupdates/${DEBUGLIB}/${SITELIB}/block/$$x.debug"\
#		  "$$x.${VERSION}" ; \
#		echo install -m 0755 $$x.${VERSION} \
#			${DESTDIR}/RHupdates/block/$$x ; \
#		install -m 0755 $$x.${VERSION} \
#			${DESTDIR}/RHupdates/block/$$x ; \
#	done
#
#install : all
#	@install -v -m 0755 -d ${DESTDIR}/${SITELIB}/block/
#	@for x in ${PYFILES} ; do \
#		install -v -m 0755 $$x ${DESTDIR}/${SITELIB}/block/$$x ; \
#	done
#	@for x in ${LIBS} ; do \
#		install -v -m 0755 $$x.${VERSION} \
#			${DESTDIR}/${SITELIB}/block/$$x ; \
#	done
#
#clean :
#	@rm -vf *.py[co] *.so *.so.${VERSION} *.o core* *.pyc *.pyo
#	@rm -vf pyblock-*.tar.bz2

.PHONY : clean install
.SECONDARY : ${foreach lib,${LIBS},lib${lib}.so.${VERSION}}
