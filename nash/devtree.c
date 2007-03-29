/*
 * devtree.c -- represent the tree of device nodes, both found and needed
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

#define _GNU_SOURCE 1

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <nash.h>
#include "list.h"
#include "util.h"
#include "devtree.h"
#include "lib.h"
#include "dm.h"

extern int nash_bdev_vitals_get_type(struct nash_bdev_vitals *);

extern void nash_dev_node_free_ptr(nashContext *nc, 
    struct nash_dev_node **nodep);
static struct nash_dev_node *nash_dev_node_from_blkent(nashContext *nc,
    struct blkent *blkent);
static struct nash_dev_node *nash_dev_node_from_bdev(nashContext *nc,
    struct nash_block_dev *bdev);
static void nash_dev_node_emancipate_from_parents(nashContext *nc,
    struct nash_dev_node *child);
static void nash_dev_node_surrender_children(nashContext *nc, 
    struct nash_dev_node *adult);
static int nash_dev_node_viable_parent(nashContext *nc,
        struct nash_dev_node *adult,
        struct nash_dev_node *child);

extern int nash_bdev_vitals_type_cmp(struct nash_bdev_vitals *v0,
    struct nash_bdev_vitals *v1);
extern int nash_bdev_vitals_cmp(struct nash_bdev_vitals *v0,
    struct nash_bdev_vitals *v1);
/*
 * First: the structure that goes in nashContext and its accessors
 */

struct nash_dev_tree; /* actually it's in the header now */

typedef int (*nash_dev_tree_visitor)(nashContext *nc,
    struct nash_dev_node *node,
    void *priv);


int nash_dev_node_visit_children_helper(nashContext *nc,
    struct nash_dev_node *adult, int visit_adult, nash_dev_tree_visitor visitor,
    struct nash_list *to_visit, void *priv)
{
    struct nash_list_iter *i;
    struct nash_dev_node *child;
    int rc;
    int pos;

    rc = 0;

    pos = nash_list_search(to_visit, adult);
    if (visit_adult && pos >= 0) {
        nash_list_remove(to_visit, pos);
        rc += visitor(nc, adult, priv);
    }

    i = NULL;
    while ((i = nash_list_iter_next(adult->children, i))) {
        child = nash_list_iter_data(i);
        if ((pos = nash_list_search(to_visit, child)) >= 0) {
            rc += nash_dev_node_visit_children_helper(nc, child, 1, visitor,
                to_visit, priv);
        }
    }

    return rc;
}

int nash_dev_node_visit_children(nashContext *nc, struct nash_dev_node *adult, 
    int visit_adult, nash_dev_tree_visitor visitor, void *priv)
{
    struct nash_dev_tree *tree = adult->tree;
    struct nash_list *to_visit;
    int errnum;
    int rc;

    if (!(to_visit = nash_ptr_list_new()))
        return -1;

    struct nash_list_iter *i;
    i = NULL;
    while ((i = nash_list_iter_next(tree->devs, i))) {
        struct nash_dev_node *node = nash_list_iter_data(i);
        nash_list_add(to_visit, node);
    }

    rc = nash_dev_node_visit_children_helper(nc, adult, visit_adult, visitor,
        to_visit, priv);

    save_errno(errnum, nash_list_destroy(to_visit));
    return rc;
}

void nash_dev_tree_delete_node(nashContext *nc, struct nash_dev_node *node)
{
    struct nash_dev_tree *tree = node->tree;
    int pos;

    nash_dev_node_emancipate_from_parents(nc, node);
    nash_dev_node_surrender_children(nc, node);
    if (node->type == DEV_TYPE_FS) {
        if ((pos = nash_list_search(tree->fs_devs, node)) >= 0)
            nash_list_remove(tree->fs_devs, pos);
    }

    if ((pos = nash_list_search(tree->devs, node)) >= 0)
        nash_list_remove(tree->devs, pos);
}

static int attach_parents_visitor(nashContext *nc, struct nash_dev_node *adult, 
    void *childp)
{
    struct nash_dev_node *child = childp;

    if (nash_list_in(child->parents, adult))
        return 0;
    if (nash_dev_node_viable_parent(nc, adult, child)) {
        nash_list_add(adult->children, child);
        nash_list_add(child->parents, adult);
        nash_dev_tree_check_node_complete(nc, child);
        return 1;
    }
    return 0;
}

static int attach_parents(nashContext *nc, struct nash_dev_tree *tree)
{
    struct nash_list_iter *adults, *children;
    struct nash_dev_node *adult, *child;
    int ret, changed, rc;

    ret = 0;
    do {
        changed = ret;

        children = NULL;
        while (changed == ret &&
                (children = nash_list_iter_next(tree->devs, children))) {
            child = nash_list_iter_data(children);
            adults = NULL;
            while (changed == ret &&
                    (adults = nash_list_iter_next(tree->devs, adults))) {
                adult = nash_list_iter_data(adults);
                rc = nash_dev_node_visit_children(nc, adult, 1, 
                    attach_parents_visitor, child);
                ret += rc;
                if (changed != ret)
                    break;
            }
            if (adults)
                nash_list_iter_end(adults);
        }
        if (children)
            nash_list_iter_end(children);
    } while (changed != ret);
    return ret;
}

static int print_dev_tree_visitor(nashContext *nc, struct nash_dev_node *child, 
    void *priv)
{
    struct nash_list_iter *iter;

    printf("node: %s ", child->name);
    if (child->mate)
        printf("mated to %s", child->mate->name);
    printf("\n");

    if (nash_list_len(child->vitals) > 0) {
        int printed = 0;
        iter = NULL;
        while ((iter = nash_list_iter_next(child->vitals, iter))) {
            struct nash_bdev_vitals *vitals = nash_list_iter_data(iter);
            switch (vitals->type) {
                case DEV_TYPE_FS:
                    if (!printed) {
                        printf("  vitals: \n");
                        printed = 1;
                    }
                    printf("    unique_id: %s type: %s label: %s\n",
                            vitals->unique_id, vitals->fs_type, vitals->fs_label);
                    continue;
                case DEV_TYPE_LVM2_LV:
                    if (!printed) {
                        printf("  vitals: \n");
                        printed = 1;
                    }
                    printf("    unique_id: %s lv_name: %s\n", vitals->unique_id,
                            vitals->lv_name);
                    continue;
                case DEV_TYPE_LVM2_VG:
                    if (!printed) {
                        printf("  vitals: \n");
                        printed = 1;
                    }
                    printf("    unique_id: %s vg_name: %s\n", vitals->unique_id,
                            vitals->vg_name);
                    continue;
                case DEV_TYPE_LVM2_PV:
                    if (!printed) {
                        printf("  vitals: \n");
                        printed = 1;
                    }
                    printf("    unique_id: %s\n", vitals->unique_id);
                    continue;
                case DEV_TYPE_DISK:
                    if (!printed) {
                        printf("  vitals: \n");
                        printed = 1;
                    }
                    printf("    unique_id: %s\n", vitals->unique_id);
                    continue;
            }
        }
    }
    if (child->type != DEV_TYPE_DISK && child->type != DEV_TYPE_DM_ERROR) {
        if (nash_list_len(child->parent_names) > 0) {
            printf("  parent names: ");
            iter = NULL;
            while ((iter = nash_list_iter_next(child->parent_names, iter))) {
                char *name = nash_list_iter_data(iter);
                printf("%s ", name);
            }
            printf("\n");
        }
        if (nash_list_len(child->parents) > 0) {
            printf("  parents: ");
            iter = NULL;
            while ((iter = nash_list_iter_next(child->parents, iter))) {
                struct nash_dev_node *node = nash_list_iter_data(iter);
                printf("%s ", node->name);
            }
            printf("\n");
        }
    }
    if (nash_list_len(child->child_names) > 0) {
        printf("  child names: ");
        iter = NULL;
        while ((iter = nash_list_iter_next(child->child_names, iter))) {
            char *name = nash_list_iter_data(iter);
            printf("%s ", name);
        }
        printf("\n");
    }
    if (nash_list_len(child->children) > 0) {
        printf("  children: ");
        iter = NULL;
        while ((iter = nash_list_iter_next(child->children, iter))) {
            struct nash_dev_node *node = nash_list_iter_data(iter);
            printf("%s ", node->name);
        }
        printf("\n");
    }

    return 0;
}

void print_dev_tree(nashContext *nc, int blktab)
{
    struct nash_dev_node *root = blktab ? nc->blktab->root : nc->devices->root;
    printf("printing %s tree\n", blktab ? "blktab": "device");
    nash_dev_node_visit_children(nc, root, 0, print_dev_tree_visitor, NULL);
}

static int ok_type_combo(struct nash_dev_node *left,
        struct nash_dev_node *right)
{
    int type0 = left->type, type1 = right->type;
    if (type0 == DEV_TYPE_TREE || type1 == DEV_TYPE_TREE)
        return 0;

    if (type0 == DEV_TYPE_NONE || type1 == DEV_TYPE_NONE)
        return 0;

    switch (type0) {
        case DEV_TYPE_DISK:
        case DEV_TYPE_DM_ERROR:
            if (type1 == DEV_TYPE_DISK || type1 == DEV_TYPE_DM_ERROR)
                return 1;
            return 0;
        case DEV_TYPE_DM:
            switch (type1) {
                case DEV_TYPE_DM:
                case DEV_TYPE_DM_MPATH:
                case DEV_TYPE_LVM2_LV:
                case DEV_TYPE_DM_RAID:
                    return 1;
                case DEV_TYPE_PARTITION:
                    if (right->flags & DEV_FLAG_DM)
                        return 1;
            }
            return 0;
        case DEV_TYPE_DM_MPATH:
            if (type1 == DEV_TYPE_DM || type1 == DEV_TYPE_DM_MPATH)
                return 1;
            return 0;
        case DEV_TYPE_LVM2_LV:
            if (type1 == DEV_TYPE_DM || type1 == DEV_TYPE_LVM2_LV)
                return 1;
            return 0;
        case DEV_TYPE_DM_RAID:
            if (type1 == DEV_TYPE_DM || type1 == DEV_TYPE_DM_RAID)
                return 1;
            return 0;
        case DEV_TYPE_PARTITION:
            switch (type1) {
                case DEV_TYPE_DM:
                    if (left->flags & DEV_FLAG_DM)
                        return 1;
                    break;
                case DEV_TYPE_PARTITION:
                    if (left->flags & DEV_FLAG_DM && right->flags & DEV_FLAG_DM)
                        return 1;
                    if (!(left->flags & DEV_FLAG_DM)
                            && !(right->flags & DEV_FLAG_DM))
                        return 1;
            }
            return 0;
        case DEV_TYPE_LVM2_PV:
        case DEV_TYPE_LVM2_VG:
        case DEV_TYPE_MD_RAID:
        case DEV_TYPE_FS:
            if (left->type == right->type)
                return 1;
    }
    return 0;
}

static int tree_propogate_flags(nashContext *nc, struct nash_dev_node *child, 
    void *priv)
{
    struct nash_list_iter *iter;

    iter = NULL;
    while ((iter = nash_list_iter_next(child->parents, iter))) {
        struct nash_dev_node *parent = nash_list_iter_data(iter);

        if (parent->type == DEV_TYPE_TREE || parent->type == DEV_TYPE_NONE)
            continue;
        if (parent->flags & DEV_FLAG_ACCESS_OK) {
            child->flags |= DEV_FLAG_ACCESS_OK;
            child->flags &= ~DEV_FLAG_PROTECTED;
        }
    }
    return 0;
}

static int nash_dev_node_try_mate(nashContext *nc, struct nash_dev_node *dev,
        struct nash_dev_node *tab)
{
    struct nash_list_iter *vi0;
    const dev_t ndev = makedev(-1,-1);
    int agree;
    int rc;

    if (tab->tree != nc->blktab)
        printf("table device %s has the wrong tree\n", tab->name);
    if (dev->tree != nc->devices)
        printf("probed device %s has the wrong tree\n", dev->name);

    if (tab->mate || dev->mate)
        return tab->mate == dev->mate;

    /* probably unnecessary */
    if (tab->devno != ndev && dev->devno != ndev &&
            tab->devno != dev->devno)
        return 0;

    /* probably unnecessary */
    if (tab->sysfs_path && dev->sysfs_path &&
            strcmp(tab->sysfs_path, dev->sysfs_path))
        return 0;

    /* probably unnecessary */
    if (tab->dev_path && dev->dev_path &&
            strcmp(tab->dev_path, dev->dev_path))
        return 0;

    if (!ok_type_combo(tab, dev))
        return 0;

#if 0
    printf("in try_mate\n");
#endif
    vi0 = NULL;
    agree = 0;
    while ((vi0 = nash_list_iter_next(dev->vitals, vi0))) {
        struct nash_list_iter *vi1 = NULL;
        struct nash_bdev_vitals *v0 = nash_list_iter_data(vi0);
#if 0
        printf("got dev vitals (type=%d)\n", v0->type);
#endif

        while ((vi1 = nash_list_iter_next(tab->vitals, vi1))) {
            struct nash_bdev_vitals *v1 = nash_list_iter_data(vi1);
#if 0
            printf("got table vitals (type=%d)\n", v1->type);
#endif
            if ((rc = nash_bdev_vitals_type_cmp(v0, v1))) {
#if 0
                printf("nash_bdev_vitals_type_cmp(%d, %d) = %d\n", v0->type,
                        v1->type, rc);
#endif
                continue;
            }
            rc = nash_bdev_vitals_cmp(v0, v1);

            if (rc != 0) {
                agree = -1;
                nash_list_iter_end(vi1);
                break;
            } else if (agree >= 0) {
                agree++;
            }
        }
        if (agree < 0) {
            nash_list_iter_end(vi0);
            break;
        }
    }
    if (agree < 1)
        return 0;
#if 0
    printf("mating %s to %s (agree = %d)\n", tab->name, dev->name, agree);
#endif
    tab->mate = dev;
    dev->mate = tab;

    dev->flags |= tab->flags;
    if (dev->flags & DEV_FLAG_ACCESS_OK) {
        dev->flags &= ~DEV_FLAG_PROTECTED;
        if (dev->type == DEV_TYPE_DISK)
            tab->flags |= DEV_FLAG_COMPLETE;
    }

    nash_dev_node_visit_children(nc, dev, 0, tree_propogate_flags, NULL);
    return 1;
}

/* XXX this algorithm sucks ass.  need to make these be hashed on their
 * vital signs instead. */
static int nash_dev_tree_group_nodes(nashContext *nc,
        struct nash_dev_node *node)
{
    struct nash_dev_tree *tree = nc->blktab;
    struct nash_list_iter *iter;
    int rc = 0;

    iter = NULL;
    while ((iter = nash_list_iter_next(tree->devs, iter))) {
        struct nash_dev_node *other = nash_list_iter_data(iter);
        if (nash_dev_node_try_mate(nc, node, other)) {
            rc = 1;
            nash_list_iter_end(iter);
            break;
        }
    }
    iter = NULL;
    while (rc && (iter = nash_list_iter_next(node->children, iter))) {
        struct nash_dev_node *child = nash_list_iter_data(iter);
        if (!(child->flags & DEV_FLAG_COMPLETE))
            nash_dev_tree_group_nodes(nc, child);
        nash_dev_tree_check_node_complete(nc, child);
    }

    return rc;
}

static int nash_dev_tree_add_node(nashContext *nc, int blktab,
        struct nash_dev_node *node)
{
    struct nash_dev_tree *tree = blktab ? nc->blktab : nc->devices;
    struct nash_dev_node *root = tree->root;

    /*
     * If it's a filesystem, we need to add it to the fs list, and then put
     *   it into the tree at as low of a level as is allowed.
     * 
     * If anything topology changed under a disk, we need to tell our caller.
     */
    nash_list_add(tree->devs, node);
    if (node->type == DEV_TYPE_DISK || node->type == DEV_TYPE_DM_ERROR) {
        nash_list_add(root->children, node);
        nash_dev_tree_check_node_complete(nc, node);
        node->flags |= DEV_FLAG_IN_TREE;
    } else {
        if (node->type == DEV_TYPE_FS)
            nash_list_add(tree->fs_devs, node);
    }
    return attach_parents(nc, tree);
}

int nash_dev_tree_add_blkent(nashContext *nc, struct blkent *blkent)
{
    struct nash_dev_node *node;

    if (!(node = nash_dev_node_from_blkent(nc, blkent)))
        return -1;

    return nash_dev_tree_add_node(nc, 1, node);
}

int nash_dev_tree_add_bdev(nashContext *nc, struct nash_block_dev *bdev)
{
    struct nash_dev_node *node;
    char *path;
    int errnum;
    int rc;

    path = nashDmDevGetName(bdev->devno);
    if (path) {
        free(bdev->dev_path);
        bdev->dev_path = path;
    }

    if (!(node = nash_dev_node_from_bdev(nc, bdev)))
        return -1;

    if (node->type == DEV_TYPE_DISK)
        nash_dev_tree_group_nodes(nc, node);
    nash_dev_tree_check_node_complete(nc, node);

    if ((rc = nash_dev_tree_add_node(nc, 0, node)) < 0) {
        save_errno(errnum, nash_dev_node_free_ptr(nc, &node));
        return rc;
    }

    return 0;
}

int nash_dev_tree_remove_bdev(nashContext *nc, struct nash_block_dev *bdev)
{
    return -1;
}

void nash_dev_tree_free_ptr(nashContext *nc, struct nash_dev_tree **treep)
{
    struct nash_dev_tree *tree = *treep;
    struct nash_list_iter *i;

    if (!tree)
        return;

    if (tree->root)
        nash_dev_node_free_ptr(nc, &tree->root);
    i = NULL;
    if (tree->devs) {
        while ((i = nash_list_iter_next(tree->devs, i))) {
            struct nash_dev_node *node;

            node = nash_list_iter_data(i);
            nash_dev_node_free_ptr(nc, &node);
        }
    }
    if (tree->devs)
        nash_list_destroy(tree->devs);
    if (tree->fs_devs)
        nash_list_destroy(tree->fs_devs);

    memset(tree, '\0', sizeof (tree));
    free(tree);

    *treep = NULL;
}

struct nash_dev_tree *nash_dev_tree_alloc(nashContext *nc)
{
    struct nash_dev_tree *dev_tree;
    int errnum;

    struct blkent be = {
        .blk_name = "devtree", 
        .blk_type = "devtree",
        .blk_opts = "",
    };

    if (!(dev_tree = calloc(1, sizeof (*dev_tree))))
        return NULL;

    if (!(dev_tree->devs = nash_ptr_list_new())) {
err:
        save_errno(errnum, nash_dev_tree_free_ptr(nc, &dev_tree));
        return NULL;
    }

    if (!(dev_tree->fs_devs = nash_ptr_list_new()))
        goto err;

    if (!(dev_tree->root = nash_dev_node_from_blkent(nc, &be)))
        goto err;

    dev_tree->root->tree = dev_tree;

    return dev_tree;
}

/* 
 * Second: info about a device node
 */


static const char *nash_dev_type_names[] = {
    [DEV_TYPE_TREE] = "devtree",
    [DEV_TYPE_DM_ERROR] = "dm-error",
    [DEV_TYPE_DISK] = "disk",
    [DEV_TYPE_DM] = "dm",
    [DEV_TYPE_DM_MPATH] = "dm-mpath",
    [DEV_TYPE_PARTITION] = "part",
    [DEV_TYPE_LVM2_PV] = "lvm2-pv",
    [DEV_TYPE_LVM2_VG] = "lvm2-vg",
    [DEV_TYPE_LVM2_LV] = "lvm2-lv",
    [DEV_TYPE_DM_RAID] = "dmraid",
    [DEV_TYPE_MD_RAID] = "md",
    [DEV_TYPE_FS] = "fs",
    [DEV_TYPE_FLOPPY] = "floppy",
    [DEV_TYPE_NONE] = "invalid",
};

const char *nash_dev_type_name(int type)
{
    if (type < DEV_TYPE_TREE || type > DEV_TYPE_NONE)
        return NULL;
    return nash_dev_type_names[type];
}

/* XXX should have vitals.h */
extern void nash_bdev_vitals_free(struct nash_bdev_vitals *);

void nash_dev_node_free_ptr(nashContext *nc, struct nash_dev_node **nodep)
{
    struct nash_dev_node *node = *nodep;
    struct nash_bdev_vitals *vitals;

    if (!node)
        return;

    xfree(node->name);

    if (node->vitals) {
        while ((vitals = nash_list_get(node->vitals, 0))) {
            nash_bdev_vitals_free(vitals);
            nash_list_remove(node->vitals, 0);
        }
        nash_list_destroy(node->vitals);
    }

    xfree(node->sysfs_path);
    xfree(node->dev_path);
    
    nash_dev_tree_delete_node(nc, node);

    if (node->parents)
        nash_list_destroy(node->parents);
    if (node->parent_names)
        nash_list_destroy(node->parent_names);
    if (node->children)
        nash_list_destroy(node->children);
    if (node->child_names)
        nash_list_destroy(node->child_names);

    xfree(node);
    *nodep = NULL;
}

static inline struct nash_dev_node *nash_dev_node_alloc(nashContext *nc,
    char *name)
{
    struct nash_dev_node *node;
    int errnum;

    if (!(node = calloc(1, sizeof (*node))))
        return NULL;

    if (name && !(node->name = strdup(name))) {
err:
        save_errno(errnum, nash_dev_node_free_ptr(nc, &node));
        return NULL;
    }
    if (!(node->vitals = nash_ptr_list_new()))
        goto err;

    node->devno = makedev(-1,-1);

    return node;
}

int nash_dev_node_get_type_from_blkent(struct blkent *blkent)
{
    int i;
    for (i = DEV_TYPE_TREE; i < DEV_TYPE_NONE; i++)
        if (!strcmp(blkent->blk_type, nash_dev_type_name(i)))
            return i;
    return i;
}

static struct nash_dev_node *nash_dev_node_from_blkent(nashContext *nc,
        struct blkent *blkent)
{
    struct nash_dev_node *node;
    struct nash_bdev_vitals *vitals;
    int errnum;
    char *name = blkent ? blkent->blk_name : NULL;

    if (!(node = nash_dev_node_alloc(nc, name)))
        return NULL;

    node->tree = nc->blktab;

    node->flags = DEV_FLAG_ACCESS_OK | DEV_FLAG_MISSING;

    node->type = nash_dev_node_get_type_from_blkent(blkent);

    if (!(vitals = nash_bdev_vitals_from_blkent(nc, blkent))) {
err:
        save_errno(errnum, nash_dev_node_free_ptr(nc, &node));
        return NULL;
    }

    if (nash_list_add(node->vitals, vitals) < 0)
        goto err;

    if (!(node->parents = nash_ptr_list_new()))
        goto err;
    if (!(node->parent_names = nash_str_list_new()))
        goto err;

    if (!(node->children = nash_ptr_list_new()))
        goto err;
    if (!(node->child_names = nash_str_list_new()))
        goto err;

    if (blkent) {
        char *start;

        errno = 0;
        if ((start = dupblkopt(blkent->blk_opts, "devs"))) {
            if (errno != 0)
                goto err;
            node->parent_names = nash_str_list_from_string(start, ",");
            save_errno(errnum, free(start));
            if (!node->parent_names)
                goto err;
        }
    }
    return node;
}

static int devno_cmp(dev_t left, dev_t right)
{
    int x;

    if ((x = major(right) - major(left)))
        return x;
    x = minor(right);
    if (x == 0)
        return 0;
    return x - minor(left);
}

static int looks_like_partition(char *path) {
    char *dev_path = strrchr(path, '/');

    if (!dev_path || !*dev_path || !*(++dev_path))
        return 0;
    for (; *dev_path && isalpha(*dev_path); dev_path++)
        ;
    if (!isdigit(*dev_path))
        return 0;
    for (; *dev_path; dev_path++) {
        if (!isdigit(*dev_path))
            return 0;
    }
    return 1;
}

static int looks_like_p_partition(char *path) {
    char *lastp = strrchr(path, 'p');

    if (!lastp)
        return 0;
    while (*(++lastp)) {
        if (!isdigit(*lastp))
            return 0;
    }
    return 1;
}

static inline int sysfs_path_cmp(struct nash_dev_node *adult,
    struct nash_dev_node *child, int longest)
{
    int l,m;
    if (!adult->sysfs_path || !child->sysfs_path)
        return 0;
    l = strlen(adult->sysfs_path);
    m = strlen(child->sysfs_path);
    l = longest ? MAX(l,m) : MIN(l,m);
    if (!strncmp(adult->sysfs_path, child->sysfs_path, l))
        return 1;
    return 0;
}
static int is_sysfs_parent(struct nash_dev_node *adult,
        struct nash_dev_node *child)
{
    return sysfs_path_cmp(adult, child, 0);
}
static int is_sysfs_node(struct nash_dev_node *adult,
        struct nash_dev_node *child)
{
    return sysfs_path_cmp(adult, child, 1);
}

/* FIXME -ldmraid currently won't get us a list of formats.  That needs to
 * be fixed, and then this needs to query from it. */
static int looks_like_dmraid(char *path) {
    char *lasts = strrchr(path, '/');
    static const char *prefixes[] = {
        "asr_", "hpt37x_", "hpt45x_", "isw_", "jmicron_", "lsi_",
        "nvidia_", "pdc_", "sil_", "via_", "ddf1_", ""
    };
    const char *prefix;

    if (!lasts || !*(++lasts))
        return 0;
    for (prefix = prefixes[0]; prefix[0] != '\0'; prefix++) {
        if (!strncmp(lasts, prefix, strlen(prefix)))
            return 1;
    }
    return 0;
}

/* XXX hahahahahahah this is terrible.  But there's no way to ask a dm
 * rule if it's lvm at this point... well, sortof.
 *
 * So, this *really* needs to go after all the other (better) checks...
 */
static int looks_like_lvm(char *path) {
    char *lasts = strrchr(path, '/');

    if (!lasts || !(++lasts))
        return 0;
    if (strchr(lasts, '-'))
        return 1;
    return 0;
}

int nash_dev_node_guess_type_from_bdev(struct nash_dev_node *node)
{
    static const struct known_dev_types {
        char *name;
        int type;
        enum {
            NO_P_CONVENTION,
            USES_P_CONVENTION,
            NONE,
        } part;
        char *prefix;
    } known_dev_types[] = {
        /* these names needs to match the list in util.h */
        { "ataraid", DEV_TYPE_DISK, USES_P_CONVENTION, "ataraid/"},/*XXX?*/
        { "cciss", DEV_TYPE_DISK, USES_P_CONVENTION, "cciss/" },
        { "dac960", DEV_TYPE_DISK, USES_P_CONVENTION, "rd/" },
        { "device-mapper", DEV_TYPE_DM, USES_P_CONVENTION, "mapper/" },
        { "floppy", DEV_TYPE_FLOPPY, NONE, "" },
        { "i2o_block", DEV_TYPE_DISK, NO_P_CONVENTION, "i2o/" },
        { "ida", DEV_TYPE_DISK, USES_P_CONVENTION, "ida/" },
        { "ide", DEV_TYPE_DISK, NO_P_CONVENTION, "" },
        { "iseries/vd", DEV_TYPE_DISK, NO_P_CONVENTION, "iseries/" },
#if 0 || defined(NASH_SUPPORT_ABSURD_DEVICE_TOPOLOGY)
        { "md", DEV_TYPE_MD_RAID, USES_P_CONVENTION, "" },
        { "mdp", DEV_TYPE_PARTITION, NONE, "" },
#else
        { "md", DEV_TYPE_MD_RAID, NONE, "" },
#endif
        { "sd", DEV_TYPE_DISK, NO_P_CONVENTION, "" },
        { "sx8", DEV_TYPE_DISK, USES_P_CONVENTION, "sx8/" },
        { NULL, 0 }
    }, *k;
    int type;    
    char *name;
    dev_t devno;
    int m;

    devno = node->devno;
    m = -1;
    do {
        name = NULL;
        if ((m = getDevsFromProc(m, S_IFBLK, &name, &devno)) < 0)
            break;
        for (k = &known_dev_types[0];
                k->name != NULL && k->name[0] <= name[0];
                k++) {
            if (!strcmp(name, k->name)) {
                type = k->type;
                if (k->part == NO_P_CONVENTION) {
                    if (looks_like_partition(node->dev_path))
                        type = DEV_TYPE_PARTITION;
                } else if (k->part == USES_P_CONVENTION) {
                    if (k->type == DEV_TYPE_DM) {
                        char *dmtype;
                        if (!node->dev_path)
                            return type;
                        dmtype = nashDmDevGetType(node->devno);
                        if (dmtype && !strcmp(dmtype, "error"))
                            type = DEV_TYPE_DM_ERROR;
                        else if (looks_like_p_partition(node->dev_path))
                            type = DEV_TYPE_PARTITION;
                        else if (looks_like_dmraid(node->dev_path))
                            type = DEV_TYPE_DM_RAID;
                        else if (looks_like_lvm(node->dev_path))
                            type = DEV_TYPE_LVM2_LV;
                        else
                            type = DEV_TYPE_DM_MPATH;
                    } else {
                        if (looks_like_partition(node->dev_path))
                            type = DEV_TYPE_PARTITION;
                    }
                }
                return type;
            }
        }
    } while (1);

    return DEV_TYPE_NONE;
}

static int nash_dev_node_add_vitals_from_bdev(
        nashContext *nc, struct nash_dev_node *node)
{
    struct nash_list *probed_vitals;
    struct nash_bdev_vitals *vitals;

    node->type = nash_dev_node_guess_type_from_bdev(node);

    if (!(probed_vitals = nash_vitals_probe(nc, node)))
        return 0;
    while ((vitals = nash_list_get(probed_vitals, 0))) {
        if (!nash_list_in(node->vitals, vitals))
            nash_list_add(node->vitals, vitals);
        nash_list_remove(probed_vitals, 0);
    }
    nash_list_destroy(probed_vitals);
    return 1;
}

static struct nash_dev_node *nash_dev_node_from_bdev(nashContext *nc,
        struct nash_block_dev *bdev)
{
    struct nash_dev_node *node;
    int errnum;

    if (!(node = nash_dev_node_alloc(nc, bdev->sysfs_path)))
        return NULL;

    node->tree = nc->devices;

    node->flags = DEV_FLAG_PROTECTED | DEV_FLAG_CHILDREN_MISSING;

    node->dev_path = bdev->dev_path;
    node->sysfs_path = bdev->sysfs_path;
    node->devno = bdev->devno;
    memset(bdev, '\0', sizeof(*bdev));

    if (!(node->parents = nash_ptr_list_new()))
        goto err;
    if (!(node->parent_names = nash_str_list_new()))
        goto err;

    if (!(node->children = nash_ptr_list_new()))
        goto err;
    if (!(node->child_names = nash_ptr_list_new()))
        goto err;

#if 0
    printf("getting vitals for %s\n", node->sysfs_path);
#endif
    nash_dev_node_add_vitals_from_bdev(nc, node);

    return node;
err:
    save_errno(errnum, nash_dev_node_free_ptr(nc, &node));
    return NULL;
}

/*
 * the heraldry concept here is that the closer it is to a bare disk,
 * the more adult it is.  adults provide resources (e.g. blocks) for
 * their children.
 *
 * That is to say, nothing has a disk as a child, and filesystems do not
 * have children.
 *
 * The table can be read as "a CHILD can reside on MIN to MAX ADULTs"
 * MAX of  0 means it's an invalid combination
 * MAX of -1 means unbounded
 *
 * child of type DEV_TYPE_NONE means end-of-list
 */
static const struct devtree_constraint {
    nash_dev_type child;
    nash_dev_type adult;
    int min_parents;
    int max_parents;
} devtree_constraints[] = {
    { DEV_TYPE_DISK, DEV_TYPE_TREE, 0, -1 },
    { DEV_TYPE_DM_MPATH, DEV_TYPE_DISK, 2, -1 },
    { DEV_TYPE_DM_RAID, DEV_TYPE_DISK, 1, -1 },
    { DEV_TYPE_PARTITION, DEV_TYPE_DISK, 1, 1 },
    { DEV_TYPE_PARTITION, DEV_TYPE_DM_MPATH, 1, 1 },
    { DEV_TYPE_PARTITION, DEV_TYPE_DM_RAID, 1, 1 },
#if 0 || defined(NASH_SUPPORT_ABSURD_DEVICE_TOPOLOGY)
    { DEV_TYPE_PARTITION, DEV_TYPE_MD_RAID, 1, 1 },
#endif
    { DEV_TYPE_MD_RAID, DEV_TYPE_PARTITION, 1, -1 },
#if 0 || defined(NASH_SUPPORT_ABSURD_DEVICE_TOPOLOGY)
    { DEV_TYPE_LVM2_PV, DEV_TYPE_DISK, 1, 1 },
    { DEV_TYPE_LVM2_PV, DEV_TYPE_DM_MPATH, 1, 1 },
    { DEV_TYPE_LVM2_PV, DEV_TYPE_DM_RAID, 1, 1 },
    { DEV_TYPE_LVM2_PV, DEV_TYPE_MD_RAID, 1, 1 },
#endif
    { DEV_TYPE_LVM2_PV, DEV_TYPE_PARTITION, 1, 1 },
    { DEV_TYPE_LVM2_PV, DEV_TYPE_MD_RAID, 1, 1 },
    { DEV_TYPE_LVM2_VG, DEV_TYPE_LVM2_PV, 1, -1 },
    { DEV_TYPE_LVM2_LV, DEV_TYPE_LVM2_VG, 1, 1 },
#if 0 || defined(NASH_SUPPORT_ABSURD_DEVICE_TOPOLOGY)
    { DEV_TYPE_FS, DEV_TYPE_DISK, 1, 1 },
    { DEV_TYPE_FS, DEV_TYPE_DM_MPATH, 1, 1 },
    { DEV_TYPE_FS, DEV_TYPE_DM_RAID, 1, 1 },
#endif
    { DEV_TYPE_FS, DEV_TYPE_LVM2_LV, 1, 1 },
    { DEV_TYPE_FS, DEV_TYPE_PARTITION, 1, 1 },
    { DEV_TYPE_FS, DEV_TYPE_MD_RAID, 1, 1 },
    { DEV_TYPE_NONE, DEV_TYPE_NONE, 0, 0 }
};

static int nash_dev_node_viable_parent(nashContext *nc,
        struct nash_dev_node *adult,
        struct nash_dev_node *child)
{
    int i;
    struct nash_dev_node *parent;
    int num_named_parents = 0;
    int num_found_parents = 0;

    if (child == adult)
        return 0;

//    printf("testing viability of %s and %s\n", adult->name, child->name);
    num_named_parents = -1;
    if (child->tree == nc->blktab)
        num_named_parents = nash_list_len(child->parent_names);
    num_found_parents = nash_list_len(child->parents);

    if (num_named_parents == 0 || num_named_parents == num_found_parents)
        return 0;

    parent = nash_list_get(child->parents, 0);

    for (i = 0; devtree_constraints[i].child != DEV_TYPE_NONE; i++) {
        const struct devtree_constraint *con = &devtree_constraints[i];

        if (con->child != child->type || con->adult != adult->type)
            continue;
        if (con->max_parents == 0)
            continue;
        if (num_found_parents > 0 && parent) {
            /* if there are already parents, but they're not this type,
             * then this can't be it... */
            if (con->adult != parent->type)
                continue;

            if (con->max_parents != -1 && con->max_parents <= num_found_parents)
                continue;
        }

        if (adult->tree == nc->devices) {
            if (is_sysfs_parent(adult, child)) {
#if 0
                printf("%s identified as parent of %s via sysfs\n",
                        adult->name, child->name);
#endif
                return 1;
            }
        } else {
            if (nash_list_in(child->parent_names, adult->name)) {
#if 0
                printf("%s identified as parent of %s\n", adult->name,
                        child->name);
#endif
                return 1;
            }
        }
    }
    return 0;
}

/* purpose: remove child from its parents */
static void nash_dev_node_emancipate_from_parents(nashContext *nc,
    struct nash_dev_node *child)
{
    struct nash_dev_node *adult;
    int pos;

    while ((adult = nash_list_get(child->parents, 0))) {
        pos = nash_list_search(adult->children, child);
        nash_list_remove(adult->children, pos);
        nash_list_remove(child->parents, 0);
        if (adult->tree == nc->devices)
            adult->flags &= ~DEV_FLAG_CHILDREN_MISSING;
    }
    child->flags &= ~(DEV_FLAG_IN_TREE);
}

/* purpose: remove adult's children from adult */
static void nash_dev_node_surrender_children(nashContext *nc, 
    struct nash_dev_node *adult)
{
    struct nash_dev_node *child;
    int i;

    while ((child = nash_list_get(adult->children, 0))) {
        i = nash_list_search(child->parents, adult);
        nash_list_remove(child->parents, i);
        nash_list_remove(adult->children, 0);
        child->flags &= ~DEV_FLAG_IN_TREE;
        if (child->tree == nc->devices)
            child->flags &= ~DEV_FLAG_PARENTS_MISSING;
    }
}

char *nash_dev_node_get_dm_name(nashContext *nc, char *name)
{
    struct nash_dev_node *node;
    char *dmname = NULL;
    int i = -1;
    int blktab = 0;
    
    if (!strncmp(name, "blktab=", 7)) {
        name += 7;
        blktab = 1;
    } else if (name[0] == '$') {
        name += 1;
        blktab = 1;
    }
    node = nash_dev_tree_find_device(nc, name, blktab);
    while (nash_dev_node_vitals_get(nc, node, "label", ++i, &dmname) >= 0) {
        if (dmname)
            break;
    }
    if (!dmname)
        dmname = name;
    return dmname;
}

/*
 * Third: loading the blktab from disk.
 * XXX maybe should be 4th?
 */

static struct nash_list *nash_load_blkents(nashContext *nc, const char *path);
static void nash_free_blkents(struct nash_list *blkents);

int nash_load_blktab(nashContext *nc, const char *path)
{
    struct nash_list *blkents = NULL;
    struct nash_list_iter *i = NULL;
    int j = 0;

    if (!(blkents = nash_load_blkents(nc, path)))
        return -1;

    while ((i = nash_list_iter_next(blkents, i))) {
        struct blkent *blkent = nash_list_iter_data(i);
        if (nash_dev_tree_add_blkent(nc, blkent) >= 0)
            j++;
    }

    nash_free_blkents(blkents);

    return j;
}

static void nash_free_blkents(struct nash_list *blkents)
{
    struct blkent *blkent;

    while ((blkent = nash_list_get(blkents, 0))) {
        nash_list_remove(blkents, 0);
        free(blkent);
    }
}

static struct nash_list *nash_load_blkents(nashContext *nc, const char *path)
{
    FILE *btfile = NULL;
    struct nash_list *blkents;
    struct blkent *blkent = NULL;
    int errnum;

    if (!(btfile = setblkent(path, "r")))
        return NULL;

    if (!(blkents = nash_ptr_list_new()))
        goto blkents_err;

    while ((blkent = getblkent(btfile)) != NULL) {
        int name_len = strlen(blkent->blk_name) + 1;
        int type_len = strlen(blkent->blk_type) + 1;
        int opts_len = strlen(blkent->blk_opts) + 1;
        struct blkent *newent;

        if (!(newent = calloc(1, sizeof (*newent) \
                + name_len + type_len + opts_len)))
            goto blkents_err;

        newent->blk_name = (void *)newent + sizeof(*newent);
        memmove(newent->blk_name, blkent->blk_name, name_len);

        newent->blk_type = newent->blk_name + name_len;
        memmove(newent->blk_type, blkent->blk_type, type_len);

        newent->blk_opts = newent->blk_type + type_len;
        memmove(newent->blk_opts, blkent->blk_opts, opts_len);

        nash_dev_tree_add_blkent(nc, newent);
    }

    endblkent(btfile);
    return blkents;
blkents_err:
    save_errno(errnum, nash_free_blkents(blkents); endblkent(btfile));
    return NULL;
}

/*
 * fourth section:
 * seeing if we have complete info for a device, and if so, building it.
 */
/*
 * return codes are as follows:
 * 1 == can build (or at least we think so ;)
 * 0 == can partially build (e.g. for md, dmraid, or mpath)
 * -1 == no way at all.
 */
/* XXX this really only checks if all the DEV_TYPE_DISK devices are present */
int nash_dev_tree_check_node_complete(nashContext *nc,
        struct nash_dev_node *child)
{
    int rc = 1;
    struct nash_dev_node *adult;
    int i, l0, l1;

    if (child->flags & DEV_FLAG_COMPLETE) {
#if 0
        printf("node %s is flagged complete, not checking\n", child->name);
#endif
        return 1;
    }
#if 0
    printf("checking for node %s's parents\n", child->name);
#endif
    switch (child->type) {
        case DEV_TYPE_TREE:
        case DEV_TYPE_NONE:
            return -1;
        case DEV_TYPE_DISK:
#if 0
            printf("node %s has status of %d\n", child->name, child->mate ? 1 : -1);
#endif
            if (child->mate == NULL)
                return -1;
            child->flags |= DEV_FLAG_COMPLETE;
            return 1;
        case DEV_TYPE_DM_MPATH:
        case DEV_TYPE_DM_RAID:
        case DEV_TYPE_MD_RAID:
        case DEV_TYPE_PARTITION:
        case DEV_TYPE_LVM2_VG:
        case DEV_TYPE_LVM2_LV:
        case DEV_TYPE_FS:
        default:
            i = -1;
            while (rc >= 0 && (adult = nash_list_get(child->parents, ++i))) {
               int ret = nash_dev_tree_check_node_complete(nc, adult);
               if (rc > ret)
                   rc = ret;
               if (rc < 0)
                   break;
            }
            if (rc >= 0) {
                l0 = nash_list_len(child->parent_names);
                l1 = nash_list_len(child->parents);
                if (l0 == l1)
                    rc = MIN(rc, 1);
                else if (l0)
                    rc = MIN(rc, 0);
                else
                    rc = -1;
            }
#if 0
            printf("node %s has status of %d\n", child->name, rc);
#endif
            if (rc == 1)
                child->flags |= DEV_FLAG_COMPLETE;
            return rc;
    }
    return -1;
}

/*
 * find a tree node given the label, uuid, or path
 */
struct nash_dev_node *nash_dev_tree_find_device(nashContext *nc, char *device,
        int blktab)
{
    int i;
    struct nash_dev_node *child, *adult;
    char namebuf[1024] = {'\0'};
    enum {
        LABEL,
        UUID,
        PATH,
    } type = PATH;

    i = -1;
    adult = NULL;

    if (blktab) {
        while (!adult && (child = nash_list_get(nc->blktab->devs, ++i))) {
            if (!strcmp(child->name, device)) {
                adult = child;
#if 0
                printf("found adult %s\n", adult->name);
#endif
                break;
            }
        }
    } else {
        char *target = device;
        if (!strncasecmp(device, "label=", 6)) {
            type = LABEL;
            target += 6;
        } else if (!strncasecmp(device, "uuid=", 5)) {
            type = UUID;
            target += 5;
        }

        if (type != PATH) {
            while (!adult &&
                    (child = nash_list_get(nc->devices->fs_devs, ++i))) {
                if (nash_dev_node_vitals_has(nc, child,
                        type == LABEL ? "label" : "unique_id", target))
                    adult = child;
            }
            while (!adult &&
                    (child = nash_list_get(nc->devices->devs, ++i))) {
                if (nash_dev_node_vitals_has(nc, child,
                        type == LABEL ? "label" : "unique_id", target))
                    adult = child;
            }
        } else {
            while (!adult && (child = nash_list_get(nc->devices->devs, ++i))) {
                char *slash, *lvname;
                if ((child->dev_path && !strcmp(child->dev_path, device)) ||
                        (child->mate && child->mate->dev_path &&
                         !strcmp(child->mate->dev_path, device))) {
                    adult = child;
                }
                if (!adult && (lvname = strrchr(device, '/'))) {
                    slash = strchr(device, '/');
                    strncpy(namebuf, slash, MIN(lvname-slash, 1023));
                    lvname++;
                    if (child->type == DEV_TYPE_LVM2_LV &&
                            !strcmp(child->name, lvname)) {
                        child = nash_list_get(child->parents, 0);
                        if (child->type == DEV_TYPE_LVM2_VG &&
                                !strcmp(child->name, namebuf))
                            adult = child;
                    }
                }
            }
        }
    }

    return adult;
}

static int nash_dev_node_setup_parents(nashContext *nc,
    struct nash_dev_node *child)
{
    int rc = 1;

    if (child->type == DEV_TYPE_DISK) {
#if 0
        if (!child->mate)
            nash_dev_tree_group_nodes(nc, child);
#endif
        if (child->mate) {
            child->flags |= DEV_FLAG_COMPLETE;
            return 1;
        }
        return 0;
    } else {
        struct nash_dev_node *parent;
        int i = -1;
        while ((parent = nash_list_get(child->parents, ++i))) {
            int ret = nash_dev_node_setup_parents(nc, parent);
            if (ret < rc)
                rc = ret;
        }
    }
    return rc;
}

int nash_dev_tree_setup_device(nashContext *nc, char *device)
{
    struct nash_dev_node *node = NULL;
    int blktab = 0;

    if (!strncmp(device, "blktab=", 7)) {
        device += 7;
        blktab = 1;
    } else if (device[0] == '$') {
        device += 1;
        blktab = 1;
    }

    node = nash_dev_tree_find_device(nc, device, blktab);
    if (!node)
        return -1;
    return nash_dev_node_setup_parents(nc, node);
}

char *nash_dev_tree_replace_dm_params(nashContext *nc, long long start,
        long long length, char *params)
{
    struct nash_dev_node *node;
    char *ret = NULL, *tmp = NULL;
    char *prev = params, *dollar;

    while ((dollar = strchr(prev, '$'))) {
        int l = strcspn(dollar, " \t\n");
        char *dmname = NULL;
        dev_t devno;
        int devmaj, devmin;
        char c;

        c = dollar[l];
        dollar[0] = '\0';
        dollar[l] = '\0';

        node = nash_dev_tree_find_device(nc, dollar+1, 1);

        if (node->mate) {
            devno = node->mate->devno;
        } else {
            dmname = node->name;
            nashDmCreate(nc, dmname, NULL, start, length, "error", "");
            node->flags |= DEV_FLAG_COMPLETE;

            devno = nashDmGetDev(dmname);
        }
        devmaj = major(devno);
        devmin = minor(devno);

        asprintf(&tmp, "%s%s%d:%d", ret ? ret : "", prev, devmaj, devmin);
        dollar[0] = '$';
        
        if (ret)
            free(ret);
        ret = tmp;
        dollar[l] = c;
        prev = dollar + l;
    }
    if (ret) {
        if (prev[0] != '\0') {
            asprintf(&tmp, "%s%s", ret, prev);
            free(ret);
            ret = tmp;
        }
    } else {
        ret = strdup(prev);
    }
    return ret;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
