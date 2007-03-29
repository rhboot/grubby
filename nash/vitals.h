/*
 * vitals.h -- represent the vital information (i.e. uniquiely identifying
 *             remarks, model number, etc.) for a disk
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2007 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef NASH_PRIV_VITALS_H
#define NASH_PRIV_VITALS_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <nash.h>

struct nash_bdev_vitals {
    int refcount;
    int type;
    char *name;

    union {
        /* DEV_TYPE_DISK */
        struct {
            char *unique_id;
            char *disk_device_node;
            char *disk_model;
            char *disk_vendor;
            char *disk_probe_module;
            char *disk_probe_name;
        };
        /* DEV_TYPE_MPATH, DEV_TYPE_DMRAID */
        struct {
            char *dmname;
        };
        /*  DEV_TYPE_LVM2_PV */
        struct {
            char *unique_id;
        };
        /* nothing for DEV_TYPE_PARTITION */
        /* DEV_TYPE_LVM2_VG */
        struct {
            char *unique_id;
            char *vg_name;
        };
        /* DEV_TYPE_LVM2_LV */
        struct {
            char *unique_id;
            char *lv_name;
        };
        /* DEV_TYPE_LVM2_FS */
        struct {
            char *unique_id;
            char *fs_type;
            char *fs_label;
            char *fs_options;
        };
        /* XXX FIXME: DEV_TYPE_MD_RAID */
    };
};

extern void nash_bdev_vitals_free(struct nash_bdev_vitals *vitals);
extern void nash_vitals_incref(struct nash_bdev_vitals *vitals);
extern void nash_vitals_decref(struct nash_bdev_vitals *vitals);
extern struct nash_bdev_vitals *nash_bdev_vitals_alloc(void);
extern struct nash_bdev_vitals *nash_bdev_vitals_from_blkent(nashContext *nc,
    struct blkent *blkent);
extern int nash_vitals_initialize_probes(void);
extern void nash_vitals_destroy_probes(void);
struct nash_list *nash_vitals_probe(nashContext *nc,
        struct nash_dev_node *node);

#endif /* NASH_PRIV_VITALS_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
