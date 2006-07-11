/*
 * block.h -- functions for dealing with block devices.
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
#ifndef NASH_BLOCK_H
#define NASH_BLOCK_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <sys/stat.h>

extern void nashBlockInit(nashContext *);
extern void nashBlockFinish(nashContext *);

extern char *nashFindFsByLabel(nashContext *, const char *label);
extern char *nashFindFsByUUID(nashContext *, const char *uuid);
extern char *nashFindFsByName(nashContext *, const char *name);
extern char *nashFindDeviceByDevno(dev_t devno);

extern int nashParseSysfsDevno(const char *path, dev_t *dev);

extern int nashDisablePartitions(const char *devname);

extern int nashMkPathBySpec(nashContext *c, const char *spec, const char *path);
extern char *nashAGetPathBySpec(nashContext *c, const char * spec);
#define nashGetPathBySpec(_ctx, _spec) ({       \
        char *_ret0, *_ret1 = NULL;             \
        _ret0 = nashAGetPathBySpec(_ctx, _spec);\
        if (_ret0) {                            \
            _ret1 = strdupa(_ret0);             \
            free(_ret0);                        \
        }                                       \
        _ret1;                                  \
    })

typedef struct nash_block_dev *nashBdev;
typedef struct nash_block_dev_iter *nashBdevIter;

extern nashBdevIter nashBdevIterNew(const char *path);
extern void nashBdevIterEnd(nashBdevIter *iter);
extern int nashBdevIterNext(nashBdevIter iter, nashBdev *dev);

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_BLOCK_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
