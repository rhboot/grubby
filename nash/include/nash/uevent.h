/*
 * uevent.h -- reads uevents from the kernel
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
 */

#ifndef NASH_UEVENT_H
#define NASH_UEVENT_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <sys/poll.h>
#include <time.h>

typedef struct nash_uevent_handler *nashUEventHandler;

typedef struct nash_uevent {
    char *msg;
    char *path;
    char *envz;
    size_t envz_len;
} nashUEvent;

extern struct nash_uevent_handler *nashUEventHandlerNew(nashContext *);
extern int nashGetUEventPoll(nashUEventHandler handler,
        struct timespec *timeout, nashUEvent *uevent,
        struct pollfd *, int npfds);
#define nashGetUEvent(handler, timeout, uevent) \
    nashGetUEventPoll(handler, timeout, uevent, NULL, 0)
extern void _nashUEventHandlerDestroy(nashUEventHandler *handler); 
#define nashUEventHandlerDestroy(handler) _nashUEventHandlerDestroy(&(handler))
extern int nashUEventHandlerGetFd(struct nash_uevent_handler *handler);

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_UEVENT_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
