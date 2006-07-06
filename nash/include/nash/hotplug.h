/*
 * hotplug.h -- hotplug loading/hotplug event handling
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * vim:ts=8:sw=4:sts=4:et
 */

#ifndef NASH_HOTPLUG_H
#define NASH_HOTPLUG_H 1

extern int nashHotplugInit(struct nash_context *);
extern void nashHotplugNewRoot(struct nash_context *);
extern void nashHotplugNotifyExit(struct nash_context *);
extern void nashHotplugKill(struct nash_context *);

typedef int (*nashFileFetcher_t)(char *inpath, int outfd);

extern void nashSetFileFetcher(struct nash_context *, nashFileFetcher_t);
extern nashFileFetcher_t nashGetFileFetcher(struct nash_context *);
extern int nashSetFirmwarePath(struct nash_context *, char *);
extern char *nashGetFirmwarePath(struct nash_context *);

#endif /* NASH_HOTPLUG_H */
