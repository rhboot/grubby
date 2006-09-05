/*
 * Copyright 2006 Red Hat, Inc.
 *
 * Authors:
 *    Peter Jones <pjones@redhat.com>
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
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <bdevid.h>

static inline char *
usb_get_sysfs_device(struct bdevid_bdev *bdev)
{
    char *path = NULL, *device = NULL, *tmp = NULL;

    /* Ewww.  This whole function is just disgusting.  It might not even
       work on devices other than my test drive.  No idea. */

    if (!(path = bdevid_bdev_get_sysfs_dir(bdev)))
        return NULL;
    if (asprintf(&device, "%s/device", path) < 0)
        return NULL;

    path = canonicalize_file_name(device);
    free(device);
    if (!path)
        return NULL;

    device = strrchr(path, '/');
    device[0] = '\0';
    device = strrchr(path, '/');
    device[0] = '\0';
    device = strrchr(path, '/');
    device[0] = '\0';
    device = strrchr(path, '/');
    device[0] = '\0';
    device = strrchr(path, '/');
    device[0] = '\0';
    device = strrchr(path, '/');

    if (strncmp(device, "/usb", 4)) {
        free(path);
        return NULL;
    }
    device[strlen(device)] = '/';
    device[strlen(device)] = '/';

    if (asprintf(&tmp, "%s/driver", path) < 0) {
        free(path);
        return NULL;
    }
    device = canonicalize_file_name(tmp);
    free(tmp);
    if (!device) {
        free(path);
        return NULL;
    }
    tmp = strrchr(device, '/');
    if (strcmp(tmp, "/usb-storage")) {
        free(path);
        free(device);
        return NULL;
    }
    free(device);

    device = strrchr(path, '/');
    device[0] = '\0';

    device = NULL;

    return path;
}

static inline int
usb_get_devattr(struct bdevid_bdev *bdev, char *attr, char **value)
{
    char *device = NULL, *path = NULL;
    FILE *f = NULL;
    char buf[1024] = "";
    int rc = -1;
    
    if (!(device = usb_get_sysfs_device(bdev)))
        return -1;

    if (asprintf(&path, "%s/%s", device, attr) < 0)
        goto err;

    if (!(f = fopen(path, "r")))
        goto err;

    free(path);
    path = NULL;

    if (!fgets(buf, 1023, f))
        goto err;

    if ((*value = strndup(buf, 1023)))
        rc = 0;
err:
    if (path)
        free(path);
    if (device)
        free(device);
    if (f)
        fclose(f);

    return rc;
}

static int usb_get_vendor(struct bdevid_bdev *bdev, char **vendor)
{
    return usb_get_devattr(bdev, "manufacturer", vendor);
}

static int usb_get_model(struct bdevid_bdev *bdev, char **model)
{
    return usb_get_devattr(bdev, "product", model);
}

static int usb_get_unique_id(struct bdevid_bdev *bdev, char **id)
{
    return usb_get_devattr(bdev, "serial", id);
}

static struct bdevid_probe_ops usb_probe_ops = {
    .name = "usb_probe",
    .get_vendor = usb_get_vendor,
    .get_model = usb_get_model,
    .get_unique_id = usb_get_unique_id,
};

static int usb_module_init(struct bdevid_module *bm)
{
    if (bdevid_register_probe(bm, &usb_probe_ops) == -1)
        return -1;
    return 0;
}

static struct bdevid_module_ops usb_module = {
    .magic = BDEVID_MAGIC,
    .name = "usb",
    .init = usb_module_init,
};

BDEVID_MODULE(usb_module);

/*
 * vim:ts=8:sw=4:sts=4:et
 */
