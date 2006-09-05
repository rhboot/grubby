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

struct nashProbeResult {
    char *module;
    char *probe;
    char *vendor;
    char *model;
    char *unique_id;

    struct nashProbeResult *next;
};
typedef struct nashProbeResult nashProbeResult;

struct nashDevice {
    int entry;

    nashBdev bdev;

    struct nashProbeResult *probe;

    struct blkent *blkent;
    int assembled;
};
typedef struct nashDevice nashDevice;

struct priv {
    nashContext *nc;
    nashDevice *dev;
};

static inline void
_probe_free(nashProbeResult **pp)
{
    nashProbeResult *p, *n;

    if (!pp || !*pp)
        return;
    p = *pp;
    do {
        n = p->next;
        if (p->module)
            free(p->module);
        if (p->probe)
            free(p->probe);
        if (p->vendor)
            free(p->vendor);
        if (p->model)
            free(p->model);
        if (p->unique_id)
            free(p->unique_id);
        free(p);
        p = n;
    } while (p);
    *pp = NULL;
}

#define probe_free(p) _probe_free(&(p))

static inline int
probe_cmp(nashProbeResult *a, struct nashProbeResult *b)
{
    int x;
    if ((x = strcmp(a->module, b->module)))
        return x;
    if ((x = strcmp(a->probe, b->probe)))
        return x;
    if ((x = strcmp(a->vendor, a->vendor)))
        return x;
    if ((x = strcmp(a->model, a->model)))
        return x;
    return 0;
}

static int
bdevidProbeVisitor(struct bdevid_probe_result *result, void *priv)
{
    char *attrs[] = {"module", "probe", "vendor", "model", "unique_id", NULL};
    int i;
    nashProbeResult *p;
    struct priv *private = priv;
    nashDevice *dev = private->dev;

    if (!(p = calloc(1, sizeof (*p))))
        return -1;

    for (i = 0; attrs[i]; i++) {
        const char *key = attrs[i];
        const char *value = NULL;
        char *v2 = NULL;

        if (!(value = bdevid_pr_getattr(result, key)))
            continue;
        v2 = strdup(value);
        if (!strcmp(key, "module"))
            p->module = v2;
        else if (!strcmp(key, "probe"))
            p->probe = v2;
        else if (!strcmp(key, "vendor"))
            p->vendor = v2;
        else if (!strcmp(key, "model"))
            p->model = v2;
        else if (!strcmp(key, "unique_id"))
            p->unique_id = v2;
        else if (v2)
            free(v2);
    }
    if (!p->module || !p->probe) {
        probe_free(p);
        return 0;
    }

    if (dev->probe) {
        nashProbeResult *node = dev->probe;

        while (node->next) {
            if (probe_cmp(node, p)) {
                probe_free(p);
                return 0;
            }
            node = node->next;
        }
        node->next = p;
    } else
        dev->probe = p;

    return 0;
}

enum match_level {
    MATCH_NONE,
    MATCH_PARTIAL,
    MATCH_FULL,
};

static int
addDevice(nashContext *nc, struct blkent **blkents, nashBdev bdev)
{
    nashDevice **devs = nc->devs;
    nashDevice *dev = NULL;
    struct bdevid *bdevid = nashBdevidInit(nc);
    int i;

    struct priv priv = {
        .nc = nc,
    };

    if (devs) {
        for (i = 0; devs[i]->entry != 0; i++) {
            if (!nashBdevCmp(bdev, devs[i]->bdev))
                break;
        }
        if (devs[i]->entry != 0)
            dev = devs[i];
    } else {
        if (!(devs = calloc(1, sizeof (dev))))
            return -1;
        if (!(devs[0] = calloc(1, sizeof (*dev)))) {
            free(devs);
            return -1;
        }
        devs[0]->probe = NULL;
        devs[0]->bdev = NULL;
        devs[0]->entry = 0;
        nc->devs = devs;
    }

    if (!dev) {
        if (!(dev = calloc(1, sizeof (*dev))))
            return -1;
        dev->entry = devs[0]->entry + 1;
        if (!(dev->bdev = nashBdevDup(bdev))) {
            free(dev);
            return -1;
        }
        if (!(devs = realloc(devs, sizeof (dev) * (dev->entry+1))))
            /* nc->devs is consistent here; no need to do any cleanup */
            return -1;
        memmove(&devs[1], &devs[0], sizeof (dev) * (dev->entry));
        devs[0] = dev;
        nc->devs = devs;
    }

    priv.dev = dev;

    bdevid_probe(bdevid, bdev->dev_path, bdevidProbeVisitor, &priv);

    return 0;
}

enum match_level
uuidMatches(nashContext *nc, nashDevice *dev, char *opt)
{
    return MATCH_NONE;
}

static int
addDeviceBlkent(nashContext *nc, nashDevice *dev, struct blkent *blk, int final)
{
    if (!strcmp(blk->blk_type, "disk")) {
        char *opt;

        if ((opt = hasblkopt(blk, "uuid="))) {
            enum match_level ml = final ? MATCH_PARTIAL : MATCH_FULL;

            opt += strlen("uuid=");
            if (uuidMatches(nc, dev, opt) >= ml) {
                dev->blkent = blk;
                return 0;
            }
        } else if ((opt = hasblkopt(blk, "dev_path="))) {
            opt += strlen("dev_path=");
            if (!strcmp(opt, dev->bdev->dev_path)) {
                dev->blkent = blk;
                return 0;
            }
        }
    } else if (!strcmp(blk->blk_type, "dm")) {
        return -1;
    } else if (!strcmp(blk->blk_type, "fs")) {
        return -1;
    } else if (!strcmp(blk->blk_type, "swap")) {
        return -1;
    } else if (!strcmp(blk->blk_type, "lv")) {
        return -1;
    } else if (!strcmp(blk->blk_type, "vg")) {
        return -1;
    } else if (!strcmp(blk->blk_type, "pv")) {
        return -1;
    }

    return -1;
}

static int
tryAssembleDevice(nashContext *nc, struct blkent **blkents, char *device,
    int final)
{
    nashDevice **devs = nc->devs;
    int i;

    for (i = 0; devs[i]->entry != 0; i++) {
        nashDevice *dev = devs[i];

        if (!strcmp(dev->bdev->dev_path, device)) {
            if (!dev->blkent) {
                int j;

                for (j = 0; blkents[j] != NULL; j++)
                    addDeviceBlkent(nc, dev, blkents[j], final);
            }
            /* XXX do assembly here */
            if (dev->blkent) {
                char *opt = NULL;
                if (!strcmp(dev->blkent->blk_type, "disk") && 
                        (opt = hasblkopt(dev->blkent, "dev_path="))) {
                    opt += strlen("dev_path=");
                    if (!strcmp(dev->bdev->dev_path, opt)) {
                        char *path;
                        
                        smartmknod(dev->bdev->dev_path, 0600, dev->bdev->devno);
                        path = nashGetPathBySpec(nc, opt);
                        if (path)
                            dev->assembled = 1;
                    }
                }
            }
            /* should be assembled here */
            if (dev->assembled) {
                nashLogger(nc, NASH_NOTICE, "assembled %s (%d:%d)\n",
                    dev->bdev->dev_path, 
                    major(dev->bdev->devno), minor(dev->bdev->devno));
                return 0;
            }
        }
    }

    return -1;
}

int
nashWaitForDevice(nashContext *nc, struct blkent **blkents, char *device)
{
    nashDevice **devs = nc->devs;
    nashBdevIter biter;
    nashBdev dev = NULL;
    struct timespec timeout = {0, 0};
    int i;

    nashBlockInit(nc);

    if (!(biter = nashBdevIterNewPoll(nc, "/sys/block", &timeout)))
        return -1;

    while (nashBdevIterNext(biter, &dev) >= 0) {
        if (addDevice(nc, blkents, dev) < 0)
            /* even if that failed, we could still find device... */
            continue;
        if (tryAssembleDevice(nc, blkents, device, 0) >= 0) {
            nashBdevIterEnd(&biter);
            return 0;
        }
    }
    if (biter)
        nashBdevIterEnd(&biter);

    if (tryAssembleDevice(nc, blkents, device, 1) >= 0)
        return 0;

    printf("done iterating, this is a failure\n");
    
    devs = nc->devs;
    for (i = 0; devs[i]->entry != 0; i++) {
        dev_t devno = devs[i]->bdev->devno;
        printf("%s (%d:%d)\n", devs[i]->bdev->sysfs_path,
                major(devno), minor(devno));
    }

    return -1;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
