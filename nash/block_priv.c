/*
 * block.c -- find/create block devices and their fs nodes
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

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <blkid/blkid.h>

#include <nash.h>
#include "lib.h"
#include "block.h"
#include "util.h"

void
block_show_labels(nashContext *c)
{
    nashBdevIter biter;
    nashBdev dev = NULL;
    blkid_dev bdev = NULL;

    nashBlockInit(c);
    biter = nashBdevIterNew("/sys/block");
    while(nashBdevIterNext(biter, &dev) >= 0) {
        blkid_tag_iterate titer;
        const char *type, *data;
        char *label=NULL, *uuid=NULL;

        bdev = blkid_get_dev(c->cache, dev->dev_path, BLKID_DEV_NORMAL);
        if (!bdev)
            continue;
        titer = blkid_tag_iterate_begin(bdev);
        while(blkid_tag_next(titer, &type, &data) >= 0) {
            if (!strcmp(type, "LABEL"))
                label = strdup(data);
            if (!strcmp(type, "UUID"))
                uuid = strdup(data);
        }
        blkid_tag_iterate_end(titer);
        if (label) {
            printf("%s %s ", dev->dev_path, label);
            free(label);
        }
        if (uuid) {
            printf("%s", uuid);
            free(uuid);
        }
        if (label)
            printf("\n");
    }
    nashBdevIterEnd(&biter);

    nashBlockFinish(c);
}

void
sysfs_blkdev_probe(const char *dirname)
{
    nashBdevIter iter;
    nashBdev dev = NULL;

    iter = nashBdevIterNew(dirname);
    while(nashBdevIterNext(iter, &dev) >= 0)
        smartmknod(dev->dev_path, S_IFBLK | 0700, dev->devno);
    nashBdevIterEnd(&iter);
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
