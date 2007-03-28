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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>

#include <argz.h>
#include <envz.h>

#include <selinux/selinux.h>
#include <blkid/blkid.h>

#include <linux/blkpg.h>

#include <nash.h>
#include "lib.h"
#include "block.h"
#include "dm.h"

int
nashBdevRemovable(nashBdev bdev)
{
    char *subpath = NULL;
    char *contents = NULL;
    int rc;
    int fd;

    rc = asprintfa(&subpath, "%s/removable", bdev->sysfs_path);
    if (rc == -1)
        return 0;

    fd = open(subpath, O_RDONLY);
    if (fd == -1)
        return 0;

    rc = readFD(fd, &contents);
    if (rc < 0) {
        close(fd);
        return 0;
    }

    rc = contents[0] > '0';
    free(contents);
    close(fd);
    return rc;
}

int
nashParseSysfsDevno(const char *path, dev_t *dev)
{
    char *first = NULL, *second;
    int major, minor;
    int fd, len = strlen(path);
    char *devname;

    asprintf(&devname, "%s/dev", path);
    fd = open(devname, O_RDONLY);
    free(devname);
    if (fd < 0) {
        return -1;
    }

    len = readFD(fd, &first);

    close(fd);

    second = strchr(first, ':');
    *second++ = '\0';

    errno = 0;
    major = strtol(first, NULL, 10);

    errno = 0;
    minor = strtol(second, NULL, 10);
    free(first);

    *dev = makedev(major, minor);
    return 0;
}

void nashBlockInit(nashContext *c)
{
    if (blkid_get_cache(&c->cache, "/etc/blkid/blkid.tab") < 0)
        blkid_get_cache(&c->cache, NULL);
}

void nashBlockFinish(nashContext *c)
{
    blkid_put_cache(c->cache);
    c->cache = NULL;
}

void nashBdevFreePtr(nashBdev *dev)
{
    struct nash_block_dev *d;

    if (!dev || !*dev)
        return;
    d = *dev;
    if (d->sysfs_path) {
        free(d->sysfs_path);
        d->sysfs_path = NULL;
    }
    if (d->dev_path) {
        free(d->dev_path);
        d->dev_path = NULL;
    }
    d->devno = 0;
    free(d);
    *dev = NULL;
}

int
nashBdevCmp(struct nash_block_dev *a, struct nash_block_dev *b)
{
    if (a->type != b->type)
        return b->type - a->type;
    return b->devno - a->devno;
}

nashBdev
nashBdevDup(nashBdev dev)
{
    nashBdev new = NULL;
    if (!(new = calloc(1, sizeof (*new)))) {
err:
        nashBdevFree(new);
        return NULL;
    }
    if (!(new->sysfs_path = strdup(dev->sysfs_path)))
        goto err;
    if (!(new->dev_path = strdup(dev->dev_path)))
        goto err;
    new->type = dev->type;
    new->devno = dev->devno;
    return new;
}

struct nash_block_dev_iter {
    nashContext *nc;
    struct nash_block_dev_iter *parent;
    struct nash_block_dev_iter *current;

    DIR *dir;
    char *dirname;
    struct dirent *dent;
    struct timespec timeout;

    struct nash_block_dev *bdev;
};

static nashBdevIter
block_sysfs_get_top(nashBdevIter iter)
{
    while (iter->parent)
        iter = iter->parent;
    return iter;
}

static void
block_sysfs_iterate_destroy(nashBdevIter iter)
{
    if (!iter)
        return;

    if (iter->dir) {
        closedir(iter->dir);
        iter->dir = NULL;
    }
    if (iter->dirname) {
        free(iter->dirname);
        iter->dirname = NULL;
    }
}

static nashBdevIter
_nashBdevIterNew(nashContext *nc, const char *path, struct timespec *timeout)
{
    const char *dirpath = path ? path : "/sys/block";
    nashBdevIter iter = NULL;

    if (!(iter = calloc(1, sizeof(*iter))))
        return NULL;

    iter->nc = nc;
    iter->parent = NULL;
    iter->current = iter;
    iter->dent = NULL;
    iter->dirname = strdup(dirpath);

    if (timeout && (timeout->tv_sec != 0 && timeout->tv_nsec != 0)) {
        if (!nc->uh && !(nc->uh = nashUEventHandlerNew(nc)))
            goto err;
        iter->timeout = *timeout;
    } else {
        usectospec(0, &iter->timeout);
    }

    if (!(iter->dir = opendir(iter->dirname)))
        goto err;

    return iter;
err:
    block_sysfs_iterate_destroy(iter);
    free(iter);
    return NULL;
}

nashBdevIter
nashBdevIterNewPoll(nashContext *nc, const char *path,
                       struct timespec *timeout)
{
    return _nashBdevIterNew(nc, path, timeout);
}

nashBdevIter
nashBdevIterNew(nashContext *nc, const char *path)
{
    return _nashBdevIterNew(nc, path, NULL);
}

static int
block_sysfs_try_dir(nashBdevIter iter, char *sysfs_path, nashBdev *dev)
{
    int ret;
    dev_t devno = 0;
    char *bang = NULL;
    nashBdev tmp = NULL;

    if ((ret = nashParseSysfsDevno(sysfs_path, &devno)) < 0)
        return ret;

    /* we can't just assign it to *dev, or gcc decides it's unused */
    tmp = calloc(1, sizeof (struct nash_block_dev));
    tmp->type = ADD;
    tmp->devno = devno;
    tmp->sysfs_path = strdup(sysfs_path);
    asprintf(&tmp->dev_path, "/dev/%s", iter->dent->d_name);
    while ((bang = strchr(tmp->dev_path, '!')))
        *bang = '/';
    *dev = tmp;
    return 0;
}

static int
block_try_uevent(nashUEvent *uevent, nashBdev *dev)
{
    char *slash = NULL;
    int maj, min;
    nashBdev tmp;

    if (strcmp("block", envz_get(uevent->envz, uevent->envz_len, "SUBSYSTEM")))
        return -1;

    tmp = calloc(1, sizeof (struct nash_block_dev));
    if (!strcmp(uevent->msg, "add")) {
        tmp->type = ADD;
    } else if (!strcmp(uevent->msg, "remove")) {
        tmp->type = REMOVE;
    } else {
        goto err;
    }

    maj = atoi(envz_get(uevent->envz, uevent->envz_len, "MAJOR"));
    min = atoi(envz_get(uevent->envz, uevent->envz_len, "MINOR"));
    tmp->devno = makedev(maj, min);

    slash = strrchr(envz_get(uevent->envz, uevent->envz_len, "DEVPATH"), '/');

    if (asprintf(&tmp->dev_path, "/dev%s", slash) < 0)
        goto err;

    if (asprintf(&tmp->sysfs_path, "/sys%s", envz_get(uevent->envz,
            uevent->envz_len, "DEVPATH")) < 0)
        goto err;

    *dev = tmp;
    return 0;
err:
    nashBdevFree(tmp);
    return -1;
}

void
nashBdevIterEnd(nashBdevIter *iter)
{
    nashBdevIter parent;
    nashBdevIter top;

    if (!iter || !*iter)
        return;

    nashBdevFree((*iter)->bdev);

    top = block_sysfs_get_top(*iter);
    *iter = (top)->current;
    do {
        parent = (*iter)->parent;
        block_sysfs_iterate_destroy((*iter));
        free(*iter);
        *iter = parent;
    } while (*iter);
}

static int
_nashBdevUEventIter(nashBdevIter iter, nashBdev *dev, struct timespec timeout)
{
    nashUEvent uevent;
    int ret;

    memset(&uevent, '\0', sizeof (uevent));
    if (iter->dirname) {
        free(iter->dirname);
        iter->dirname = NULL;
    }

    while (1) {
        ret = nashGetUEvent(iter->nc->uh, &timeout, &uevent);
        if (ret < 0 && errno == EINTR)
            continue;
        else if (ret <= 0)
            break;
        ret = block_try_uevent(&uevent, dev);
        if (uevent.msg)
            free(uevent.msg);
        if (uevent.path)
            free(uevent.path);
        while (uevent.envz)
            argz_delete(&uevent.envz, &uevent.envz_len, uevent.envz);
        memset(&uevent, '\0', sizeof (uevent));
        if (ret >= 0)
            return 0;
    }

    return -1;
}

int
nashBdevIterNext(nashBdevIter iter, nashBdev *dev)
{
    nashBdevIter top;
    nashBdevIter parent;
    char *name;
    struct stat sb;
    struct timespec now;
    struct timespec timeout, deadline;
    int rc;

    if (!iter)
        return 0;

    nashBdevFreePtr(dev);
    iter->bdev = NULL;
    top = block_sysfs_get_top(iter);
    iter = top->current;

    gettimespecofday(&now);
    timeout = iter->timeout;
    tsadd(&now, &timeout, &deadline);
    do {
        /* if we have an open dir, we're still iterating directories */
        if (iter->dir) {
            while ((iter->dent = readdir(iter->dir)) && 
                    (!strcmp(iter->dent->d_name, ".") ||
                     !strcmp(iter->dent->d_name, "..")))
                ;

            if (iter->dent == NULL) {
                int ret;

                if (!iter->parent) {
                    if (iter->dir) {
                        closedir(iter->dir);
                        iter->dir = NULL;
                    }
                    if (iter->dirname) {
                        free(iter->dirname);
                        iter->dirname = NULL;
                    }
                    block_sysfs_iterate_destroy(iter);
                    continue;
                }
                parent = iter->parent;
                block_sysfs_iterate_destroy(iter);
                free(iter);
                iter = top->current = parent;

                asprintf(&name, "%s/%s", iter->dirname, iter->dent->d_name);
                ret = block_sysfs_try_dir(iter, name, dev);
                free(name);
                if (ret >= 0) {
                    iter->bdev = *dev;
                    return 1;
                }

                continue;
            }

            asprintf(&name, "%s/%s", iter->dirname, iter->dent->d_name);
            if (lstat(name, &sb) >= 0 && S_ISDIR(sb.st_mode)) {
                nashBdevIter newiter = nashBdevIterNew(iter->nc, name);

                if (newiter != NULL) {
                    newiter->parent = iter;
                    iter = top->current = newiter;
                }
            }
            free(name);
            continue;
        }

        /* we only ever get this far if we're done iterating directories */

        /* if the iter didn't get uevent handler during instantiation, then 
         * we're not polling on hotplug, so bail now */
        if (!iter->nc->uh)
            break;

        gettimespecofday(&now);
        if (tsGT(&now, &deadline))
            return -1;

        timeout = iter->timeout;
        if ((rc = _nashBdevUEventIter(iter, dev, timeout)) == 0)
            return 1;
        
        gettimespecofday(&now);
    } while (tsLE(&now, &deadline));
    /* if we've timed out, we don't want to say we're done */
    if (tsGT(&now, &deadline))
        return -1;
    /* and we only ever get here if we're done iterating _everything_ */
    return 0;
}

char *
nashFindDeviceByDevno(nashContext *nc, dev_t devno)
{
    nashBdevIter biter;
    nashBdev dev = NULL;
    char *path = NULL;

    biter = nashBdevIterNew(nc, "/sys/block");
    while (nashBdevIterNext(biter, &dev) > 0) {
        if (dev->devno == devno) {
            path = strdup(strrchr(dev->dev_path, '/')+1);
            break;
        }
    }
    nashBdevIterEnd(&biter);
    return path;
}

static char *
block_find_fs_by_keyvalue(nashContext *nc, const char *key, const char *value)
{
    nashBdevIter biter;
    nashBdev dev = NULL;
    blkid_dev bdev = NULL;

    biter = nashBdevIterNew(nc, "/sys/block");
    while (nashBdevIterNext(biter, &dev) > 0) {
        blkid_tag_iterate titer;
        const char *type, *data;
        char *dmname = NULL, *name = NULL;

        if (!strncmp(dev->dev_path, "/dev/dm-", 8))
            dmname = nashDmDevGetName(dev->devno);
        name = dmname ? dmname : dev->dev_path;
        bdev = blkid_get_dev(nc->cache, name, BLKID_DEV_NORMAL);
        if (dmname)
            free(dmname);
        if (!bdev)
            continue;
        titer = blkid_tag_iterate_begin(bdev);
        while(blkid_tag_next(titer, &type, &data) >= 0) {
            if (!strcmp(type, key) && !strcmp(data, value)) {
                name = strdup(blkid_dev_devname(bdev));
                blkid_tag_iterate_end(titer);
                nashBdevIterEnd(&biter);
                return name;
            }
        }
        blkid_tag_iterate_end(titer);
    }
    nashBdevIterEnd(&biter);

    return NULL;
}

char *
nashFindFsByLabel(nashContext *nc, const char *label)
{
    return block_find_fs_by_keyvalue(nc, "LABEL", label);
}

char *
nashFindFsByUUID(nashContext *nc, const char *uuid)
{
    return block_find_fs_by_keyvalue(nc, "UUID", uuid);
}

char *
nashFindFsByName(nashContext *nc, const char *name)
{
    blkid_dev bdev = NULL;

    if (!access("/sys/block", F_OK)) {
        /* populate the whole cache */
        block_find_fs_by_keyvalue(nc, "unlikely","unlikely");

        /* now look our device up */
        bdev = blkid_get_dev(nc->cache, name, BLKID_DEV_NORMAL);
    }

    if (bdev)
        return strdup(blkid_dev_devname(bdev));

    if (!access(name, F_OK))
        return strdup(name);
    return NULL;
}

char *
nashAGetPathBySpec(nashContext *nc, const char * spec)
{
    char *path;

    nashBlockInit(nc);
    if (!strncmp(spec, "LABEL=", 6))
        path = nashFindFsByLabel(nc, spec+6);
    else if (!strncmp(spec, "UUID=", 5))
        path = nashFindFsByUUID(nc, spec+5);
    else
        path = nashFindFsByName(nc, spec);
    nashBlockFinish(nc);

    return path;
}

int
nashMkPathBySpec(nashContext *nc, const char *spec, const char *path)
{
    char *existing = nashGetPathBySpec(nc, spec);
    struct stat sb;

    if (!existing || stat(existing, &sb) < 0 || !S_ISBLK(sb.st_mode))
        return -1;

    return smartmknod(path, S_IFBLK | 0700, sb.st_rdev);
}

static int
block_disable_partition(int fd, int partno)
{
    struct blkpg_partition part = {
        .pno = partno,
    };
    struct blkpg_ioctl_arg io = {
        .op = BLKPG_DEL_PARTITION,
        .datalen = sizeof(part),
        .data = &part,
    };
    int ret;

    ret = ioctl(fd, BLKPG, &io);
    if (ret < 0)
        return 0;
    return 1;
}

int
nashDisablePartitions(const char *devname)
{
    int fd;
    int partno;
    char path[256];
    int ret = 0;

    snprintf(path, 255, "/dev/%s", devname);
    fd = open(path, O_RDWR);
    if (fd < 0)
        return fd;

    for (partno = 1; partno <= 256; partno++)
        ret += block_disable_partition(fd, partno);
    return ret ? ret : -1;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
