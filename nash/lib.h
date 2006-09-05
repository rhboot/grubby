/*
 * lib.h -- a small library for various parts of nash
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

#ifndef NASH_PRIV_LIB_H
#define NASH_PRIV_LIB_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <nash.h>
#include <signal.h>
#include <blkid/blkid.h>

#include "blkent.h"

struct nashDevice;

struct nashContext_s {
    nashLogger_t logger;
    int testing;
    int quiet;
    int reallyquiet;

    blkid_cache cache;

    nashFileFetcher_t fetcher;
    char *fw_pathz;
    size_t fw_pathz_len;

    nashDelayFunction_t delayParent;
    int hp_parent_pid;
    int hp_child_pid;

    int hp_parentfd;
    int hp_childfd;

    struct bdevid *bdevid;

    void (*nashBdevidFinish)(nashContext *nc);

    struct nashDevice **devs;
};

extern int nashWaitForDevice(nashContext *, struct blkent **, char *device);

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_PRIV_LIB_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
