/*
 * vitals.c -- abstraction for the vital data regarding a block device.
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
 */

#define _GNU_SOURCE 1

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

#include <nash.h>
#include <bdevid.h>
#include "lib.h"
#include "devtree.h"

#define xstrcmp(s0,s1) (s0 ? (s1 ? strcmp(s0,s1) : -1) : (s1 ? 1 : 0))

int nash_bdev_vitals_get_type(struct nash_bdev_vitals *vitals)
{
    return vitals->type;
}

int nash_bdev_vitals_type_cmp(struct nash_bdev_vitals *v0,
    struct nash_bdev_vitals *v1)
{
    int ret;
    if (v0 == v1)
        return 0;

    /* XXX ignoring everything but disks for now */
    if (v0->type != v1->type) {
#if 0
        printf("%d != %d, returning %d\n", v0->type, v1->type, v1->type - v0->type);
#endif
        return v1->type - v0->type;
    }

    switch (v0->type) {
        case DEV_TYPE_DISK:
            ret = xstrcmp(v0->disk_probe_name, v1->disk_probe_name);
#if 0
            printf("xstrcmp(\"%s\", \"%s\") = %d\n", v0->disk_probe_name,
                    v1->disk_probe_name, ret);
#endif
            if (ret)
                return ret;
            ret = xstrcmp(v0->disk_probe_module, v1->disk_probe_module);
#if 0
            printf("xstrcmp(\"%s\", \"%s\") = %d\n", v0->disk_probe_module,
                    v1->disk_probe_module, ret);
#endif
            if (ret)
                return ret;
            break;
        default:
            /* XXX ignoring everything but disks for now */
            return -1;
    }
#if 0
    printf("returning 0 from type_cmp\n");
#endif
    return 0;
}

int nash_bdev_vitals_cmp(struct nash_bdev_vitals *v0,
    struct nash_bdev_vitals *v1)
{
    int ret = 0;

    if (v0 == v1)
        return 0;

#if 1
    if (!v0)
        printf("no v0?  wtf?\n");
    if (!v1)
        printf("no v1?  wtf?\n");

    if (!v0 || !v1)
        return v0 ? -1 : (v1 ? 1 : -1 );
#endif

    if (v1->type != v0->type)
        return v1->type - v0->type;

    switch (v0->type) {
        case DEV_TYPE_TREE:
            return -1;
        case DEV_TYPE_DISK:
            if ((ret = xstrcmp(v0->unique_id, v1->unique_id))) {
#if 0
                printf("%s: %d: xstrcmp(\"%s\", \"%s\") = %d\n", __func__,
                        __LINE__, v0->unique_id, v1->unique_id, ret);
#endif
                break;
            }
            if ((ret = xstrcmp(v0->disk_vendor, v1->disk_vendor))) {
#if 0
                printf("%s: %d: xstrcmp(\"%s\", \"%s\") = %d\n", __func__,
                        __LINE__, v0->disk_vendor, v1->disk_vendor, ret);
#endif
                break;
            }
            if ((ret = xstrcmp(v0->disk_model, v1->disk_model))) {
#if 0
                printf("%s: %d: xstrcmp(\"%s\", \"%s\") = %d\n", __func__,
                        __LINE__, v0->disk_model, v1->disk_model, ret);
#endif
                break;
            }
            if ((ret = xstrcmp(v0->disk_probe_module, v1->disk_probe_module))) {
#if 0
                printf("%s: %d: xstrcmp(\"%s\", \"%s\") = %d\n", __func__,
                        __LINE__, v0->disk_probe_module, v1->disk_probe_module,
                        ret);
#endif
                break;
            }
            ret = xstrcmp(v0->disk_probe_name, v1->disk_probe_name);
#if 0
            if (ret) {
                printf("%s: %d: xstrcmp(\"%s\", \"%s\") = %d\n", __func__,
                        __LINE__, v0->disk_probe_name, v1->disk_probe_name,
                        ret);
            }
#endif
            break;
        case DEV_TYPE_DM_MPATH:
        case DEV_TYPE_DM_RAID:
            ret = xstrcmp(v0->dmname, v1->dmname);
            break;
        case DEV_TYPE_PARTITION:
            /* XXX fixme */
            ret = -1;
            break;
        case DEV_TYPE_LVM2_PV:
            ret = xstrcmp(v0->unique_id, v1->unique_id);
            break;
        case DEV_TYPE_LVM2_VG:
            if ((ret = xstrcmp(v0->unique_id, v1->unique_id)))
                break;
            ret = xstrcmp(v0->vg_name, v1->vg_name);
            break;
        case DEV_TYPE_LVM2_LV:
            if ((ret = xstrcmp(v0->unique_id, v1->unique_id)))
                break;
            ret = xstrcmp(v0->lv_name, v1->lv_name);
            break;
        case DEV_TYPE_MD_RAID:
            /* XXX fixme */
            ret = -1;
            break;
        case DEV_TYPE_FS:
            if ((ret = xstrcmp(v0->unique_id, v1->unique_id)))
                break;
            ret = xstrcmp(v0->fs_type, v1->fs_type);
            break;
        case DEV_TYPE_NONE:
        default:
            return -1;
            break;
    }
    return ret;
}

void nash_bdev_vitals_free(struct nash_bdev_vitals *vitals)
{
    if (!vitals || --(vitals->refcount))
        return;

    xfree(vitals->name);

    switch (vitals->type) {
        case DEV_TYPE_TREE:
            break;
        case DEV_TYPE_DISK:
            xfree(vitals->unique_id);
            xfree(vitals->disk_device_node);
            xfree(vitals->disk_vendor);
            xfree(vitals->disk_model);
            xfree(vitals->disk_probe_module);
            xfree(vitals->disk_probe_name);
            break;
        case DEV_TYPE_DM_MPATH:
        case DEV_TYPE_DM_RAID:
            xfree(vitals->dmname);
            break;
        case DEV_TYPE_PARTITION:
            break;
        case DEV_TYPE_LVM2_PV:
            xfree(vitals->unique_id);
            break;
        case DEV_TYPE_LVM2_VG:
            xfree(vitals->unique_id);
            xfree(vitals->vg_name);
            break;
        case DEV_TYPE_LVM2_LV:
            xfree(vitals->unique_id);
            xfree(vitals->lv_name);
            break;
        case DEV_TYPE_MD_RAID:
            /* XXX fixme */
            break;
        case DEV_TYPE_FS:
            xfree(vitals->unique_id);
            xfree(vitals->fs_type);
            xfree(vitals->fs_label);
            xfree(vitals->fs_options);
            break;
        case DEV_TYPE_NONE:
        default:
            break;
    }
    free(vitals);
}

void nash_vitals_decref(struct nash_bdev_vitals *vitals)
    __attribute__((alias("nash_bdev_vitals_free")));

void nash_vitals_incref(struct nash_bdev_vitals *vitals)
{
    vitals->refcount++;
}

struct nash_bdev_vitals *nash_bdev_vitals_alloc(void)
{
    struct nash_bdev_vitals *vitals;

    if (!(vitals = calloc(1, sizeof (*vitals))))
        return NULL;

    vitals->type = DEV_TYPE_NONE;
    vitals->refcount = 1;
    return vitals;
}

#if 0
struct nash_bdev_vitals *nash_bdev_vitals_dedupe_helper(nashContext *nc,
        struct nash_bdev_vitals *vitals, struct nash_dev_tree *tree)
{
    struct nash_bdev_vitals *other = NULL;
    struct nash_list_iter *iter = NULL;

    if (!vitals)
        return vitals;

    while ((iter = nash_list_iter_next(tree->devs, iter))) {
        struct nash_dev_node *node = nash_list_iter_data(iter);
        struct nash_list_iter *viter = NULL;

        while ((viter = nash_list_iter_next(node->vitals, viter))) {
            other = nash_list_iter_data(viter);
            if (vitals == other) {
                other = NULL;
                continue;
            }
            if (!nash_bdev_vitals_cmp(other, vitals)) {
                nash_vitals_incref(other);
                nash_vitals_decref(vitals);
                vitals = other;
                nash_list_iter_end(viter);
                break;
            }
        }
        if (vitals == other) {
            nash_list_iter_end(iter);
            break;
        }
    }

    return vitals;
}

struct nash_bdev_vitals *nash_bdev_vitals_dedupe(nashContext *nc,
        struct nash_bdev_vitals *vitals)
{
    if (vitals->type == DEV_TYPE_DISK && !vitals->unique_id) {
        nash_vitals_decref(vitals);
        return NULL;
    }
    vitals = nash_bdev_vitals_dedupe_helper(nc, vitals, nc->blktab);
    return nash_bdev_vitals_dedupe_helper(nc, vitals, nc->devices);
}
#else
struct nash_bdev_vitals *nash_bdev_vitals_dedupe(nashContext *nc,
        struct nash_bdev_vitals *vitals)
{
    if (vitals->type == DEV_TYPE_DISK && !vitals->unique_id) {
        nash_vitals_decref(vitals);
        return NULL;
    }
    return vitals;
}
#endif

struct nash_bdev_vitals *nash_bdev_vitals_from_blkent(nashContext *nc,
        struct blkent *blkent)
{
    struct nash_bdev_vitals *vitals = nash_bdev_vitals_alloc();

    if (!vitals)
        return NULL;

    if (!blkent) {
        vitals->type = DEV_TYPE_NONE;
        return vitals;
    }

    vitals->type = nash_dev_node_get_type_from_blkent(blkent);
    if (vitals->type == DEV_TYPE_NONE) {
    //    eprintf("Invalid block device type '%s'\n", blkent->blk_type);
        goto err;
    }

    if (!(vitals->name = strdup(blkent->blk_name)))
        goto err;

    /* XXX need to check if it exists first? */
    switch (vitals->type) {
        case DEV_TYPE_DISK:
            vitals->unique_id = dupblkopt(blkent->blk_opts, "unique_id");
            vitals->disk_device_node =
                dupblkopt(blkent->blk_opts, "device_node");
            vitals->disk_vendor = dupblkopt(blkent->blk_opts, "vendor");
            vitals->disk_model = dupblkopt(blkent->blk_opts, "model");
            vitals->disk_probe_module =
                dupblkopt(blkent->blk_opts, "module");
            vitals->disk_probe_name =
                dupblkopt(blkent->blk_opts, "probe");
            if (!vitals->unique_id ||
                    !vitals->disk_device_node ||
                    !vitals->disk_vendor ||
                    !vitals->disk_model ||
                    !vitals->disk_probe_module ||
                    !vitals->disk_probe_name)
                goto err;
            break;
        case DEV_TYPE_DM_MPATH:
        case DEV_TYPE_DM_RAID:
            if (!(vitals->dmname = dupblkopt(blkent->blk_opts, "dmname")))
                goto err;
            break;
        case DEV_TYPE_PARTITION:
            /* FIXME: not even sure we really care about partitions... */
            break;
        case DEV_TYPE_LVM2_PV:
            if (!(vitals->unique_id = dupblkopt(blkent->blk_opts, "unique_id")))
                goto err;
            break;
        case DEV_TYPE_LVM2_VG:
            if (!(vitals->unique_id = dupblkopt(blkent->blk_opts, "unique_id")))
                goto err;
            if (!(vitals->vg_name = dupblkopt(blkent->blk_opts, "name")))
                goto err;
            break;
        case DEV_TYPE_LVM2_LV:
            if (!(vitals->unique_id = dupblkopt(blkent->blk_opts, "unique_id")))
                goto err;
            if (!(vitals->lv_name = dupblkopt(blkent->blk_opts, "name")))
                goto err;
            break;
        case DEV_TYPE_MD_RAID:
            /* FIXME */
            break;
        case DEV_TYPE_FS:
            if (!(vitals->unique_id = dupblkopt(blkent->blk_opts, "unique_id")))
                goto err;
            if (!(vitals->fs_type = dupblkopt(blkent->blk_opts, "type"))) 
                goto err;
            if (!(vitals->fs_label = dupblkopt(blkent->blk_opts, "label"))) 
                goto err;
            if (!(vitals->fs_options = dupblkopt(blkent->blk_opts, "options"))) 
                goto err;
            break;
        case DEV_TYPE_NONE:
            break;
    }

    return nash_bdev_vitals_dedupe(nc, vitals);
err:
    nash_vitals_decref(vitals);
    return NULL;
}

/*
 * this structure describes private probe data that relates to the probe
 * function rather than a device, so we only have to initialize probing once.
 */

struct probe_info {
    int id;
    char *name;
    void *priv;
    int (*alloc_priv)(struct probe_info *);
    void (*free_priv)(struct probe_info *);
    int (*do_probe)(nashContext *nc, struct probe_info *pi,
            struct nash_list *nodes, struct nash_bdev_vitals **vitalsp);
};

/* below this: probe functions */
/* PROBE_BDEVID prober */

static void probe_bdevid_destroy(struct probe_info *pi)
{
    struct bdevid *bdevid = pi->priv;
    if (!bdevid)
        return;

    bdevid_destroy(bdevid);
    pi->priv = NULL;
}

static int probe_bdevid_init(struct probe_info *pi)
{
    struct bdevid *bdevid;

    pi->priv = NULL;
    if (!(bdevid = bdevid_new("BDEVID_MODULE_PATH")))
        return -1;

    if (bdevid_module_load(bdevid, pi->name) < 0) {
        bdevid_destroy(bdevid);
        return -1;
    }

    pi->priv = bdevid;
    return 0;
}

struct bdevid_probe_priv {
    nashContext *nc;
    struct nash_dev_node *node;
    struct nash_bdev_vitals *vitals;
};

static int
bdevid_probe_visitor(struct bdevid_probe_result *result, void *privp)
{
    struct nash_bdev_vitals *vitals;
    struct bdevid_probe_priv *priv = (struct bdevid_probe_priv *)privp;
    char *attrs[] = {"module", "probe", "vendor", "model", "unique_id", NULL};
    int i, errnum;

    vitals = priv->vitals;
    vitals->type = DEV_TYPE_DISK;
    if (!(vitals->disk_device_node = strdup(priv->node->dev_path)))
        return -1;

    for (i = 0; attrs[i]; i++) {
        const char *key = attrs[i];
        const char *value = NULL;
        char *s = NULL;

        if (!(value = bdevid_pr_getattr(result, key)))
            continue;
        if (!(s = strdup(value)))
            goto err;
        if (!strcmp(key, "module"))
            vitals->disk_probe_module = s;
        else if (!strcmp(key, "probe"))
            vitals->disk_probe_name = s;
        else if (!strcmp(key, "vendor"))
            vitals->disk_vendor = s;
        else if (!strcmp(key, "model"))
            vitals->disk_model = s;
        else if (!strcmp(key, "unique_id"))
            vitals->unique_id = s;
        else if (s)
            free(s);
    }

    return 0;
err: 
    save_errno(errnum,
        xfree(vitals->disk_device_node);
        xfree(vitals->disk_probe_module);
        xfree(vitals->disk_probe_name);
        xfree(vitals->disk_vendor);
        xfree(vitals->disk_model);
        xfree(vitals->unique_id));

    return -1;
}

int probe_bdevid(nashContext *nc, struct probe_info *pi,
        struct nash_list *nodes, struct nash_bdev_vitals **vitalsp)
{
    struct bdevid_probe_priv priv = { .nc = nc };
    struct bdevid *bdevid;
    struct nash_bdev_vitals *vitals;
    struct nash_dev_node *node;
    int ret, errnum;

    if (!(bdevid = pi->priv))
        return -1;

    if (!(node = priv.node = nash_list_get(nodes, 0)))
        return -1;

    if (!(vitals = priv.vitals = nash_bdev_vitals_alloc()))
        return -1;

    if ((ret = bdevid_probe(bdevid, node->dev_path,
            bdevid_probe_visitor, &priv)) < 0) {
        save_errno(errnum, nash_bdev_vitals_free(vitals));
        return -1;
    }
    if (vitals->type == DEV_TYPE_NONE) {
        nash_vitals_decref(vitals);
        return -1;
    }
    *vitalsp = vitals;
    return ret;
}

/* end DEV_TYPE_DISK probe */

/* begin DEV_TYPE_DM_MPATH probe */
/* end DEV_TYPE_DM_MPATH probe */

/* begin DEV_TYPE_PARTITION probe */
/* end DEV_TYPE_PARTITION probe */

/*
 * this kindof sucks, but each probe has an ID (which can have only one bit
 * set), which are used for two purposes:
 * 1) to mark that we've done a probe we won't need to do again in
 *    node->probe_mask (not always set - dmraid for example can
 *    be used more than once, but there's still a 'final' time)
 * 2) to identify probe initialization data in probe_info[];
 */
#define PROBE_NONE          0x1 /* used for the end of the probe_info list */
#define PROBE_BDEVID_SCSI   0x2
#define PROBE_BDEVID_ATA    0x4
#define PROBE_BDEVID_USB    0x8
#define PROBE_PARTITION     0x10
#define PROBE_DM_MPATH      0x20 /* XXX might not exist... */
#define PROBE_DM_RAID       0x40
#define PROBE_MD_RAID       0x80
#define PROBE_LVM2_PV       0x100
#define PROBE_LVM2_VG       0x200
#define PROBE_LVM2_LV       0x400
#define PROBE_FS            0x800

static struct probe_info probe_info[] = {
    {
        .id = PROBE_BDEVID_SCSI,
        .name = "scsi",
        .alloc_priv = probe_bdevid_init,
        .free_priv = probe_bdevid_destroy,
        .do_probe = probe_bdevid,
    },
    {
        .id = PROBE_BDEVID_ATA,
        .name = "ata",
        .alloc_priv = probe_bdevid_init,
        .free_priv = probe_bdevid_destroy,
        .do_probe = probe_bdevid,
    },
    {
        .id = PROBE_BDEVID_USB,
        .name = "usb",
        .alloc_priv = probe_bdevid_init,
        .free_priv = probe_bdevid_destroy,
        .do_probe = probe_bdevid,
    },
#if 0
    {
        .id = PROBE_PARTITION,
        .name = "partition",
        .do_probe = probe_partitions,
    },
    /* XXX add probes for DM_MPATH, DM_RAID, MD_RAID,
     * LVM2_PV, LVM2_VG, and LVM2_LV
     */
    {
        .id = PROBE_FS,
        .name = "fs",
        .alloc_priv = probe_fs_init,
        .free_priv = probe_fs_destroy,
        .do_probe = probe_blkid,
    },
#endif
    { .id = PROBE_NONE, .name = "end of probe list" }
};

static struct probe_info *get_probe_info(int id)
{
    struct probe_info *pi = &probe_info[0];

    while (pi->id != PROBE_NONE && pi->id != id) 
        pi++;
    if (pi->id == PROBE_NONE)
        return NULL;
    return pi;
}

static int probes_initialized = 0;

int nash_vitals_initialize_probes(void)
{
    struct probe_info *pi;

    if (probes_initialized != 0)
        return probes_initialized;

    for (pi = &probe_info[0]; pi->id != PROBE_NONE; pi++) {
        /* we actually intentionally ignore failure here... maybe we shouldn't,
         * I can see both ways... */
        if (pi->alloc_priv)
            pi->alloc_priv(pi);
    }
    probes_initialized = 1;
    return 1;
}

void nash_vitals_destroy_probes(void)
{
    struct probe_info *pi;

    if (!probes_initialized)
        return;

    for (pi = &probe_info[0]; pi->id != PROBE_NONE; pi++) {
        if (pi->free_priv)
            pi->free_priv(pi);
    }
    probes_initialized = 0;
}

struct nash_list *nash_vitals_probe(nashContext *nc,
        struct nash_dev_node *node)
{
    struct probe_info *pi;
    struct nash_list *ret;
    struct nash_bdev_vitals *vitals = NULL;
    static struct nash_list *nodes;

    if (!nodes) {
        if (!(nodes = nash_ptr_list_new()))
            return NULL;
        while (nash_list_remove(nodes, 0) >= 0)
            ;
    }
    
    if (node->flags & DEV_FLAG_BANISHED)
        return NULL;

    switch (node->type) {
        case DEV_TYPE_TREE:
        case DEV_TYPE_NONE:
            return NULL;
        default:
            if (!(ret = nash_ptr_list_new()))
                return NULL;
            break;
    }

    switch (node->type) {
        case DEV_TYPE_TREE:
            /* this is here just for gcc's benefit. */
            break;
        /* this kindof sucks; it means people can't add modules themselves...*/
        case DEV_TYPE_DISK:
            nash_list_add(nodes, node);
            if (!(node->probe_mask & PROBE_BDEVID_SCSI)) {
                pi = get_probe_info(PROBE_BDEVID_SCSI);
                vitals = NULL;
                if (pi->do_probe(nc, pi, nodes, &vitals) >= 0) {
                    if ((vitals = nash_bdev_vitals_dedupe(nc, vitals)))
                        nash_list_add(ret, vitals);
                    node->probe_mask |= PROBE_BDEVID_SCSI;
                }
            }
            if (!(node->probe_mask & PROBE_BDEVID_ATA)) {
                pi = get_probe_info(PROBE_BDEVID_ATA);
                vitals = NULL;
                if (pi->do_probe(nc, pi, nodes, &vitals) >= 0) {
                    if ((vitals = nash_bdev_vitals_dedupe(nc, vitals)))
                        nash_list_add(ret, vitals);
                    node->probe_mask |= PROBE_BDEVID_ATA;
                }
            }
            if (!(node->probe_mask & PROBE_BDEVID_USB)) {
                pi = get_probe_info(PROBE_BDEVID_USB);
                vitals = NULL;
                if (pi->do_probe(nc, pi, nodes, &vitals) >= 0) {
                    if ((vitals = nash_bdev_vitals_dedupe(nc, vitals)))
                        nash_list_add(ret, vitals);
                    node->probe_mask |= PROBE_BDEVID_USB;
                }
            }
            break;
        default:
        case DEV_TYPE_NONE:
            /* this is here just for gcc's benefit. */
            break;
    }

    return ret;
}

int nash_dev_node_vitals_get(nashContext *nc,
    struct nash_dev_node *node, char *key, int i, char **value)
{
    int rc = -1;
    struct nash_bdev_vitals *vitals = nash_list_get(node->vitals, i);

    if (!vitals)
        return -1;

    if (!strcmp(key, "label")) {
        char *label = NULL;
        switch (vitals->type) {
            case DEV_TYPE_FS:
                label = vitals->fs_label;
                break;
            case DEV_TYPE_LVM2_LV:
                label = vitals->lv_name;
                break;
            case DEV_TYPE_LVM2_VG:
                label = vitals->vg_name;
                break;
            case DEV_TYPE_DM_MPATH:
            case DEV_TYPE_DM_RAID:
                label = vitals->dmname;
                break;
            default:
                rc = 0;
        }
        if (label) {
            *value = label;
            rc = 1;
        } else {
            rc = 0;
        }
    } else if (!strcmp(key, "unique_id")) {
        switch (vitals->type) {
            case DEV_TYPE_DISK:
            case DEV_TYPE_LVM2_PV:
            case DEV_TYPE_LVM2_VG:
            case DEV_TYPE_LVM2_LV:
            case DEV_TYPE_FS:
                *value = vitals->unique_id;
                rc = 1;
                break;
            default:
                rc = 0;
                break;
        }
    }
    return rc;
}

int nash_dev_node_vitals_has(nashContext *nc,
    struct nash_dev_node *node, char *key, char *value)
{
    char *name = NULL;
    int i = -1;
    int rc = 0;

    while (nash_dev_node_vitals_get(nc, node, key, ++i, &name) >= 0) {
        if (!name)
            continue;
        if (!strcmp(name, value)) {
            rc = 1;
            break;
        }
        name = NULL;
    }
    return rc;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
