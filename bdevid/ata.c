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

#include <linux/hdreg.h>

#include <bdevid.h>

static int ata_get_vendor(struct bdevid_bdev *bdev, char **vendor)
{
    char *v = strdup("1ATA");
    if (v) {
        *vendor = v;
        return 0;
    }
    return -1;
}

static int ata_get_model(struct bdevid_bdev *bdev, char **id)
{
    struct hd_driveid hdid;
    int fd;
    char model[41], rev[9];
    int i;

    fd = bdevid_bdev_get_fd(bdev);

    if (ioctl(fd, HDIO_GET_IDENTITY, &hdid) < 0)
        return -1;

    strncpy(model, (char *)hdid.model, 40);
    model[40] = '\0';
    for (i = 0; i < 40 && model[i]; ) {
        if (model[i] == ' ')
            memmove(model + i, model + i + 1, 40 - i);
        else
            i++;
    }

    strncpy(rev, (char *)hdid.fw_rev, 8);
    rev[8] = '\0';
    for (i = 0; i < 8 && rev[i]; ) {
        if (rev[i] == ' ')
            memmove(rev + i, rev + i + 1, 8 - i);
        else
            i++;
    }
    rev[i] = '\0';

    if (!model[0] && !rev[0])
        return -1;
    else if (model[0] && !rev[0]) {
        if (!(*id = strdup(model)))
            return -1;
        return 0;
    } else if (rev[0]) {
        if (asprintf(id, "%s-%s", model, rev) < 0)
            return -1;
    } else {
        if (!(*id = strdup(rev)))
            return -1;
    }
    return 0;
}

static int ata_get_unique_id(struct bdevid_bdev *bdev, char **id)
{
    struct hd_driveid hdid;
    int fd;
    char serial[21];
    char *s;
    size_t len;

    fd = bdevid_bdev_get_fd(bdev);

    if (ioctl(fd, HDIO_GET_IDENTITY, &hdid) < 0)
        return -1;

    serial[0] = '\0';
    strncpy(serial, (char *)hdid.serial_no, 20);
    for (s = serial; s[0] == ' '; s++)
        ;
    for (len = strlen(s) -1 ; len >= 0 && s[len] == ' '; len--)
        s[len] = '\0';
    if (serial != s)
        strcpy(serial, s);

    if (!serial[0])
        return -1;
    if (!(*id = strdup(serial)))
        return -1;
    return 0;
}

static struct bdevid_probe_ops ata_probe_ops = {
    .name = "ata_probe",
    .get_vendor = ata_get_vendor,
    .get_model = ata_get_model,
    .get_unique_id = ata_get_unique_id,
};

static int ata_init(struct bdevid_module *bm,
    bdevid_register_func register_probe)
{
    if (register_probe(bm, &ata_probe_ops) == -1)
        return -1;
    return 0;
}

static struct bdevid_module_ops ata_module = {
    .magic = BDEVID_MAGIC,
    .name = "ata",
    .init = ata_init,
};

BDEVID_MODULE(ata_module);

/*
 * vim:ts=8:sw=4:sts=4:et
 */
