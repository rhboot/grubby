/*
 * hotplug.h -- hotplug loading/hotplug event handling
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef NASH_HOTPLUG_H
#define NASH_HOTPLUG_H 1

extern int nashHotplugInit(nashContext *);
extern void nashHotplugNewRoot(nashContext *);
extern void nashHotplugNotifyExit(nashContext *);
extern void nashHotplugKill(nashContext *);

typedef int (*nashFileFetcher_t)(char *inpath, int outfd);
typedef void (*nashDelayFunction_t)(nashContext *, int usec);

extern void nashSetDelayParent(nashContext *, nashDelayFunction_t);
extern void nashSetFileFetcher(nashContext *, nashFileFetcher_t);
extern nashFileFetcher_t nashGetFileFetcher(nashContext *);
extern int nashSetFirmwarePath(nashContext *, char *);
extern char *nashGetFirmwarePath(nashContext *);

#endif /* NASH_HOTPLUG_H */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
