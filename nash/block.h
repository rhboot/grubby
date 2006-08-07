/*
 * block.h -- find/create block devices and their fs nodes
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

#ifndef NASH_PRIV_BLOCK_H
#define NASH_PRIV_BLOCK_H 1

#include <nash/block.h>

struct nash_block_dev {
    enum {
        ADD,
        REMOVE,
    } type;
    char *sysfs_path;
    char *dev_path;
    dev_t devno;
};

extern void block_show_labels(nashContext *c);

extern void sysfs_blkdev_probe(nashContext *c, const char *dirname);

#endif /* NASH_PRIV_BLOCK_H */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
