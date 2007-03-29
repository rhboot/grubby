/*
 * devtree.h -- represent the tree of device nodes, both found and needed
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
#ifndef NASH_PRIV_DEVTREE_H
#define NASH_PRIV_DEVTREE_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <nash.h>
#include "block.h"

#include <blkent.h>

typedef enum {
    DEV_TYPE_TREE,
    DEV_TYPE_DM_ERROR,
    DEV_TYPE_DISK,
    DEV_TYPE_DM,
    DEV_TYPE_DM_MPATH,
    DEV_TYPE_PARTITION,
    DEV_TYPE_LVM2_PV,
    DEV_TYPE_LVM2_VG,
    DEV_TYPE_LVM2_LV,
    DEV_TYPE_DM_RAID,
    DEV_TYPE_MD_RAID,
    DEV_TYPE_FS,
    DEV_TYPE_FLOPPY,
    DEV_TYPE_NONE /* end of list marker */
} nash_dev_type;

struct nash_dev_tree {
    struct nash_list *devs;
    struct nash_list *fs_devs;
    struct nash_dev_node *root;
};

struct nash_dev_node {
    char *name;
    int flags;
    nash_dev_type type;

    struct nash_dev_node *mate;

    struct nash_list *vitals;

    char *sysfs_path;
    char *dev_path;
    dev_t devno;

    struct nash_list *parents;
    struct nash_list *parent_names;
    struct nash_list *children;
    struct nash_list *child_names;

    struct nash_dev_tree *tree;

    int probe_mask;
};

#define DEV_FLAG_IN_TREE               0x1
#define DEV_FLAG_PROTECTED             0x2
#define DEV_FLAG_ACCESS_OK             0x4
#define DEV_FLAG_COMPLETE              0x8

#define DEV_FLAG_MISSING               0x10
#define DEV_FLAG_PARENTS_MISSING       0x20
#define DEV_FLAG_CHILDREN_MISSING      0x40
#define DEV_FLAG_MATCH_UNNECESSARY     0x80
#define DEV_FLAG_BANISHED              0x100 /* not sure we really need this */
#define DEV_FLAG_PROBE_WAS_BLOCKED     0x200
#define DEV_FLAG_DM                    0x400

extern struct nash_dev_tree *nash_dev_tree_alloc(nashContext *);
extern void nash_dev_tree_free_ptr(nashContext *nc,
        struct nash_dev_tree **treep);
extern int nash_dev_tree_add_bdev(nashContext *nc, struct nash_block_dev *bdev);
extern int nash_dev_tree_remove_bdev(nashContext *nc,
        struct nash_block_dev *bdev);
extern int nash_load_blktab(nashContext *nc, const char *path);

extern int nash_dev_tree_check_node_complete(nashContext *nc,
        struct nash_dev_node *child);
extern struct nash_dev_node *nash_dev_tree_find_device(nashContext *nc,
        char *device, int blktab);
extern int nash_dev_tree_setup_device(nashContext *nc, char *device);
extern char *nash_dev_tree_replace_dm_params(nashContext *nc, long long start,
        long long length, char *params);

extern const char *nash_dev_type_name(int type);
extern int nash_dev_node_get_type_from_blkent(struct blkent *blkent);
extern int nash_dev_node_add_blkent(nashContext *nc, struct blkent *blkent);
extern int nash_dev_node_add_bdev(nashContext *nc, struct nash_block_dev *bdev);
extern char *nash_dev_node_get_dm_name(nashContext *nc, char *name);
extern int nash_dev_node_vitals_has(nashContext *nc,
    struct nash_dev_node *node, char *key, char *value);
extern int nash_dev_node_vitals_get(nashContext *nc,
    struct nash_dev_node *node, char *key, int i, char **value);

#endif /* NASH_PRIV_DEVTREE_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
