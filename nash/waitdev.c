/*
 * waitdev.c
 *
 * Code to wait for and setup a specific block device.
 *
 * Peter Jones <pjones@redhat.com>
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define _GNU_SOURCE 1

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bdevid.h>

#include <nash.h>
#include "block.h"
#include "blkent.h"
#include "lib.h"
#include "util.h"

void
nashWaitCancel(nashContext *nc)
{
    if (nc->waitdev_iter)
        nashBdevIterEnd(&nc->waitdev_iter);
}

int
nashWaitForDevice(nashContext *nc, char *device, long long wait_usecs)
{
    struct timespec wait;
    int rc = -1;
    int blktab = 0;

    if (!strncmp(device, "blktab=", 7)) {
        device += 7;
        blktab = 1;
    } else if (device[0] == '$') {
        device += 1;
        blktab = 1;
    }

    usectospec(wait_usecs, &wait);

    if (!nc->waitdev_iter) {
        nashBlockInit(nc);

        nc->waitdev_iter = nashBdevIterNewPoll(nc, "/sys/block", &wait);
        if (!nc->waitdev_iter)
            return -1;
    }

    do {
        nashBdev dev = NULL;
        struct nash_dev_node *node;
        int ret;

        if ((ret = nashBdevIterNext(nc->waitdev_iter, &dev)) > 0) {
#if 0
            printf("%s bdev(%s, %s, %d:%d)\n",
                dev->type == ADD ? "add" : "remove",
                dev->sysfs_path, dev->dev_path, major(dev->devno),
                minor(dev->devno));
#endif
            
            switch (dev->type) {
                case ADD:
                    nash_dev_tree_add_bdev(nc, dev);
                    break;
                case REMOVE:
                    nash_dev_tree_remove_bdev(nc, dev);
                    break;
            }

            node = nash_dev_tree_find_device(nc, device, blktab);
            if (node && nash_dev_tree_check_node_complete(nc, node) > 0) {
                rc = 1;
                break;
            }
        } else if (ret == 0) {
            rc = 0;
            node = nash_dev_tree_find_device(nc, device, blktab);
            if (node && nash_dev_tree_check_node_complete(nc, node) > 0)
                rc = 1;
            break;
        } else {
            /* timeout */
            break;
        }
    } while (1);

    nashBdevIterEnd(&nc->waitdev_iter);
    nc->waitdev_iter = NULL;
    return rc;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
