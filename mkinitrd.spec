Summary: Creates an initial ramdisk image for preloading modules.
Name: mkinitrd
%define version 2.4.3
Version: %{version}
Release: 1
Copyright: GPL
Group: System Environment/Base
Source: mkinitrd-%{version}.tar.gz
ExclusiveArch: i386 sparc sparc64 ia64
ExclusiveOs: Linux
Requires: sash >= 3.4 e2fsprogs /bin/sh fileutils grep mount gzip tar /sbin/insmod.static /sbin/losetup
BuildRoot: %{_tmppath}/%{name}-root

%description
Mkinitrd creates filesystem images for use as initial ramdisk (initrd)
images.  These ramdisk images are often used to preload the block
device modules (SCSI or RAID) needed to access the root filesystem.

In other words, generic kernels can be built without drivers for any
SCSI adapters which load the SCSI driver as a module.  Since the
kernel needs to read those modules, but in this case it isn't able to
address the SCSI adapter, an initial ramdisk is used.  The initial
ramdisk is loaded by the operating system loader (normally LILO) and
is available to the kernel as soon as the ramdisk is loaded.  The
ramdisk image loads the proper SCSI adapter and allows the kernel to
mount the root filesystem.  The mkinitrd program creates such a
ramdisk using information found in the /etc/conf.modules file.

%prep
%setup -q

%install
rm -rf $RPM_BUILD_ROOT
make BUILDROOT=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%attr(755,root,root) /sbin/mkinitrd
%attr(644,root,root) /usr/share/man/man8/mkinitrd.8*

%changelog
* Thu Jun  1 2000 Bill Nottingham <notting@redhat.com>
- build on ia64
- bump up initrd size on ia64
- modules.confiscation, /usr/man -> /usr/share/man

* Tue May  2 2000 Nalin Dahyabhai <nalin@redhat.com>
- make RPM pick up man page, regardless of compression

* Tue Feb 29 2000 Matt Wilson <msw@redhat.com>
- add requirement for /sbin/losetup

* Mon Feb  7 2000 Matt Wilson <msw@redhat.com>
- gzip manpage

* Thu Feb  3 2000 Matt Wilson <msw@redhat.com>
- gzip manpage

* Mon Jan 10 2000 Erik Troan <ewt@redhat.com>
- use sash, not ash

* Mon Jan  3 2000 Matt Wilson <msw@redhat.com>
- use ash.static for /bin/sh, not sash

* Sat Jan 1 2000 Erik Troan <ewt@redhat.com>
- configure loopback devices

* Tue Sep 28 1999 Bill Nottingham <notting@redhat.com>
- sparc fixup from jakub

* Wed Sep 22 1999 Michael K. Johnson <johnsonm@redhat.com>
- fix cleanup (blush!)

* Tue Sep 21 1999 Michael K. Johnson <johnsonm@redhat.com>
- now works when /usr is not mounted (do not rely on /usr/bin/install)
- slight cleanups, better usage message

* Thu Jul 29 1999 Michael K. Johnson <johnsonm@redhat.com>
- Now automatically includes necessary raid modules
- --omit-raid-modules now omits raid modules
- tiny doc updates

* Thu Jul 29 1999 Bill Nottingham <notting@redhat.com>
- updates from bugzila (#4243, #4244)

* Sat Mar 27 1999 Matt Wilson <msw@redhat.com>
- --omit-scsi-modules now omits all scsi modules
- updated documentation
- mkinitrd now grabs scsi_hostadapter modules from anywhere -
  some RAID controller modules live in block/

* Thu Feb 25 1999 Matt Wilson <msw@redhat.com>
- updated description

* Mon Jan 11 1999 Matt Wilson <msw@redhat.com>
- Ignore the absence of scsi modules, include them if they are there, but
  don't complain if they are not.
- changed --no-scsi-modules to --omit-scsi-modules (as it should have been)

* Thu Nov  5 1998 Jeff Johnson <jbj@redhat.com>
- import from ultrapenguin 1.1.

* Tue Oct 20 1998 Jakub Jelinek <jj@ultra.linux.cz>
- fix for combined sparc/sparc64 insmod, also pluto module is really
  fc4:soc:pluto and we don't look at deps, so special case it.

* Sat Aug 29 1998 Erik Troan <ewt@redhat.com>
- replaced --needs-scsi-mods (which is now the default) with
  --omit-scsi-mods

* Fri Aug  7 1998 Jeff Johnson <jbj@redhat.com>
- correct obscure regex/shell interaction (hardwires tabs on line 232)

* Mon Jan 12 1998 Erik Troan <ewt@redhat.com>
- added 'make archive' rule to Makefile
- rewrote install procedure for more robust version handling
- be smarter about grabbing options from /etc/conf.modules

* Mon Oct 20 1997 Erik Troan <ewt@redhat.com>
- made it use /bin/ash.static

* Wed Apr 16 1997 Erik Troan <ewt@redhat.com>
- Only use '-s' to install binaries if /usr/bin/strip is present.
- Use an image size of 2500 if binaries can't be stripped (1500 otherwise)
- Don't use "mount -o loop" anymore -- losetup the proper devices manually
- Requires losetup, e2fsprogs

* Wed Mar 12 1997 Michael K. Johnson <johnsonm@redhat.com>
- Fixed a bug in parsing options.
- Changed to use a build tree, then copy the finished tree into the
  image after it is built.
- Added patches derived from ones written by Christian Hechelmann which
  add an option to put the kernel version number at the end of the module
  name and use install -s to strip binaries on the fly.
