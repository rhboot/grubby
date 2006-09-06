/*
 * probelib.c - provides functions for probes, so they don't have to link
 *              against the full libbdevid
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
#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>

#include "bdevid.h"
#include "priv.h"

struct bdevid_probe_result {
    struct bdevid_module *m;
    struct bdevid_probe *p;

    char *vendor;
    char *model;
    char *unique_id;
};

int
bdevid_bdev_get_fd(struct bdevid_bdev *bdev)
{
    return bdev->fd;
}

char *
bdevid_bdev_get_sysfs_dir(struct bdevid_bdev *bdev)
{
    static char buf[1024] = {0};

    snprintf(buf, 1023, "/sys/block/%s", bdev->sysfs_dir);
    return buf;
}


/*
 * vim:ts=8:sw=4:sts=4:et
 */
