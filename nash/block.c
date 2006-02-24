/*
 * block.c -- find/create block devices and their fs nodes
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
 *
 * vim:ts=8:sw=4:sts=4:et
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

#include <selinux/selinux.h>
#include <blkid/blkid.h>

#include <linux/blkpg.h>

#include "lib.h"
#include "block.h"

static int
bdev_removable(const char *path)
{
    char *subpath = NULL;
    char *contents = NULL;
    int rc;
    int fd;

    rc = asprintfa(&subpath, "%s/removable", path);
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

    rc = (contents[0] - '0') <= 0 ? 0 : 1;
    free(contents);
    close(fd);
    return rc;
}

static int
parse_sysfs_devnum(const char *path, dev_t *dev)
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

static blkid_cache cache = NULL;
static security_context_t blkid_contexts[2] = { NULL, NULL };

void
block_init(void)
{
    if (getfilecon("/etc/blkid.tab", &blkid_contexts[0]) == -1)
        getfilecon("/etc/blkid.tab.old", &blkid_contexts[0]);
    if (getfilecon("/etc/blkid.tab.old", &blkid_contexts[1]) == -1)
        getfilecon("/etc/blkid.tab", &blkid_contexts[1]);

    if (blkid_get_cache(&cache, "/etc/blkid.tab") < 0)
        blkid_get_cache(&cache, NULL);
}

void
block_finish(void)
{
    blkid_put_cache(cache);
    if (blkid_contexts[0] != NULL)
        setfilecon("/etc/blkid.tab", blkid_contexts[0]);
    if (blkid_contexts[1] != NULL)
        setfilecon("/etc/blkid.tab.old", blkid_contexts[1]);
    freecon(blkid_contexts[0]);
    freecon(blkid_contexts[1]);
    blkid_contexts[0] = blkid_contexts[1] = NULL;
}

struct block_dev {
    char *sysfs_path;
    char *dev_path;
    dev_t devno;
};
typedef struct block_dev *bdev;

static void
bdev_free(struct block_dev *dev)
{
    if (!dev)
        return;
    if (dev->sysfs_path) {
        free(dev->sysfs_path);
        dev->sysfs_path = NULL;
    }
    if (dev->dev_path) {
        free(dev->dev_path);
        dev->dev_path = NULL;
    }
    dev->devno = 0;
}

struct block_iter {
    struct block_iter *parent;
    struct block_iter *current;

    DIR *dir;
    char *dirname;
    struct dirent *dent;
};
typedef struct block_iter *bdev_iter;

static bdev_iter
block_sysfs_get_top(bdev_iter iter)
{
    while (iter->parent)
        iter = iter->parent;
    return iter;
}

static void
block_sysfs_iterate_destroy(bdev_iter iter)
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

static bdev_iter
block_sysfs_iterate_begin(const char *path)
{
    const char *dirpath = path ? path : "/sys/block";
    bdev_iter iter = calloc(1, sizeof(*iter));

    iter->parent = NULL;
    iter->current = iter;
    iter->dent = NULL;

    iter->dirname = strdup(dirpath);

    iter->dir = opendir(iter->dirname);
    if (iter->dir == NULL) {
        block_sysfs_iterate_destroy(iter);
        return NULL;
    }

    return iter;
}

static int
block_sysfs_try_dir(bdev_iter iter, char *sysfs_path, bdev *dev)
{
    int ret;
    dev_t devno = 0;

    ret = parse_sysfs_devnum(sysfs_path, &devno);

    /* don't probe floppies, cd drives, etc */
    if (bdev_removable(sysfs_path))
        return -1;
    if (ret == 0) {
        /* we can't just assign it to *dev,
           or gcc decides it's unused */
        bdev tmp = calloc(1, sizeof (struct block_dev));
        tmp->devno = devno;
        tmp->sysfs_path = strdup(sysfs_path);
        asprintf(&tmp->dev_path, "/dev/%s", iter->dent->d_name);
        smartmknod(tmp->dev_path, S_IFBLK | 0700, tmp->devno);
        *dev = tmp;
        return 0;
    }
    return -1;
}

static void
block_sysfs_iterate_end(bdev_iter *iter)
{
    bdev_iter parent;

    *iter = (*iter)->current;
    do {
        parent = (*iter)->parent;
        block_sysfs_iterate_destroy((*iter));
        free(*iter);
        *iter = parent;
    } while (*iter);
}

static int
block_sysfs_next(bdev_iter iter, bdev *dev)
{
    bdev_iter top = block_sysfs_get_top(iter);
    bdev_iter parent;
    char *name;
    struct stat sb;

    if (*dev != NULL) {
        bdev_free(*dev);
        free(*dev);
        *dev = NULL;
    }
    iter = top->current;
    do {
        iter->dent = readdir(iter->dir);

        if (iter->dent != NULL &&
                (!strcmp(iter->dent->d_name, ".")
                 || !strcmp(iter->dent->d_name, "..")))
            continue;

        if (iter->dent == NULL) {
            int ret;

            if (!iter->parent) {
                block_sysfs_iterate_destroy(iter);
                bdev_free(*dev);
                free(*dev);
                *dev = NULL;
                return -1;
            }
            parent = iter->parent;
            block_sysfs_iterate_destroy(iter);
            iter = top->current = parent;

            asprintf(&name, "%s/%s", iter->dirname, iter->dent->d_name);
            ret = block_sysfs_try_dir(iter, name, dev);
            free(name);
            if (ret >= 0)
                return 0;

            continue;
        }

        asprintf(&name, "%s/%s", iter->dirname, iter->dent->d_name);
        if (lstat(name, &sb) >= 0 && S_ISDIR(sb.st_mode)) {
            bdev_iter newiter = block_sysfs_iterate_begin(name);

            if (newiter != NULL) {
                newiter->parent = iter;
                iter = top->current = newiter;
            }
        }
        free(name);
    } while (1);
    return 0;
}

char *
block_find_device_by_devno(dev_t devno)
{
    bdev_iter biter;
    bdev dev = NULL;
    char *path = NULL;

    biter = block_sysfs_iterate_begin("/sys/block");
    while (block_sysfs_next(biter, &dev) >= 0) {
        if (dev->devno == devno) {
            path = strdup(strrchr(dev->dev_path, '/')+1);
            break;
        }
    }
    block_sysfs_iterate_end(&biter);
    return path;
}

static char *
block_find_fs_by_keyvalue(const char *key, const char *value)
{
    bdev_iter biter;
    bdev dev = NULL;
    blkid_dev bdev = NULL;
    char *name;

    biter = block_sysfs_iterate_begin("/sys/block");
    while(block_sysfs_next(biter, &dev) >= 0) {
        blkid_tag_iterate titer;
        const char *type, *data;

        bdev = blkid_get_dev(cache, dev->dev_path, BLKID_DEV_NORMAL);
        if (!bdev)
            continue;
        titer = blkid_tag_iterate_begin(bdev);
        while(blkid_tag_next(titer, &type, &data) >= 0) {
            if (!strcmp(type, key) && !strcmp(data, value)) {
                name = strdup(blkid_dev_devname(bdev));
                blkid_tag_iterate_end(titer);
                block_sysfs_iterate_end(&biter);
                return name;
            }
        }
        blkid_tag_iterate_end(titer);
    }
    block_sysfs_iterate_end(&biter);

    return NULL;
}

char *
block_find_fs_by_label(const char *label)
{
    return block_find_fs_by_keyvalue("LABEL", label);
}

char *
block_find_fs_by_uuid(const char *uuid)
{
    return block_find_fs_by_keyvalue("UUID", uuid);
}

char *
block_find_fs_by_name(const char *name)
{
    blkid_dev bdev = NULL;

    if (!access("/sys/block", F_OK)) {
        /* populate the whole cache */
        block_find_fs_by_keyvalue("unlikely","unlikely");

        /* now look our device up */
        bdev = blkid_get_dev(cache, name, BLKID_DEV_NORMAL);
    }

    if (bdev)
        return strdup(blkid_dev_devname(bdev));

    if (!access(name, F_OK))
        return strdup(name);
    return NULL;
}

void
block_show_labels(void)
{
    bdev_iter biter;
    bdev dev = NULL;
    blkid_dev bdev = NULL;

    block_init();
    biter = block_sysfs_iterate_begin("/sys/block");
    while(block_sysfs_next(biter, &dev) >= 0) {
        blkid_tag_iterate titer;
        const char *type, *data;
        char *label=NULL, *uuid=NULL;

        bdev = blkid_get_dev(cache, dev->dev_path, BLKID_DEV_NORMAL);
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
    block_sysfs_iterate_end(&biter);

    block_finish();
}

void
sysfs_blkdev_probe(const char *dirname)
{
    bdev_iter iter;
    bdev dev = NULL;

    iter = block_sysfs_iterate_begin(dirname);
    while(block_sysfs_next(iter, &dev) >= 0)
        smartmknod(dev->dev_path, S_IFBLK | 0700, dev->devno);
    block_sysfs_iterate_end(&iter);
}

char *
agetpathbyspec(const char * spec)
{
    char *path;

    block_init();
    if (!strncmp(spec, "LABEL=", 6))
        path = block_find_fs_by_label(spec+6);
    else if (!strncmp(spec, "UUID=", 5))
        path = block_find_fs_by_uuid(spec+5);
    else
        path = block_find_fs_by_name(spec);
    block_finish();

    return path;
}

int
mkpathbyspec(const char *spec, const char *path)
{
    char *existing = getpathbyspec(spec);
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
block_disable_partitions(const char *devname)
{
    int fd;
    int partno;
    char path[256];
    int ret = 0;

    snprintf(path, 255, "/dev/%s", devname);
    fd = open(path, O_RDWR);
    if (fd < 0)
        return fd;

    for (partno = 1; partno <= 256; partno++) {
        path[0]='\0';
        snprintf(path, 255, "/dev/%s%d", devname, partno);
        ret += block_disable_partition(fd, partno);
    }
    return ret ? ret : -1;
}

#if 0
int main(void)
{
#if 1
    char *dev;

    block_init();

    dev = block_find_fs_by_label("/");
    printf("dev: %s\n", dev);
    free(dev);

    block_finish();

    return 0;
#else
    blkid_cache cache;
    blkid_dev_iterate diter;
    blkid_dev dev;
    char *devname;

    blkid_get_cache(&cache, NULL);

    dev = blkid_get_dev(cache, "/dev/hda3", BLKID_DEV_NORMAL);
    diter = blkid_dev_iterate_begin(cache);
    while(blkid_dev_next(diter, &dev) >= 0) {
        blkid_tag_iterate titer;
        const char *type, *value;

        titer = blkid_tag_iterate_begin(dev);
        while(blkid_tag_next(titer, &type, &value) >= 0) {
            printf("type: %s value: %s\n", type, value);
        }
        blkid_tag_iterate_end(titer);
    }
    blkid_dev_iterate_end(diter);
#endif
}
#endif
