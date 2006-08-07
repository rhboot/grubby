/*
 * dm.c - backend library for partition table scanning on dm devices
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2005,2006 Red Hat, Inc.
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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <search.h>

#include <libdevmapper.h>
#include <parted/parted.h>

#include "dm.h"
#include "lib.h"
#include "block.h"
#include "util.h"

void
dm_cleanup(void)
{
    dm_lib_exit();
}

static inline int
nashDmTaskNew(int type, const char *name, struct dm_task **task)
{
    int ret;

    *task = dm_task_create(type);
    if (!*task)
        return 1;
    if (name)
        dm_task_set_name(*task, name);
    ret = dm_task_run(*task);
    if (ret < 0) {
        dm_task_destroy(*task);
        *task = NULL;
    }
    return ret;
}

static inline int
nashDmGetInfo(const char *name, struct dm_task **task, struct dm_info *info)
{
    int ret;

    ret = nashDmTaskNew(DM_DEVICE_INFO, name, task);
    if (ret < 0)
        return ret;

    ret = dm_task_get_info(*task, info);
    if (ret < 0 || !info->exists) {
        dm_task_destroy(*task);
        *task = NULL;
        return 1;
    }
    return 0;
}

static char *
nashDmGetType(const char *name)
{
    void *next = NULL;
    char *type = NULL;
    int ret;

    struct dm_task *task;

    ret = nashDmTaskNew(DM_DEVICE_TABLE, name, &task);
    if (ret < 0)
        return NULL;

    ret = -1;
    do {
        u_int64_t start, length;
        char *params;
        char *tmp = NULL;

        next = dm_get_next_target(task, next, &start, &length, &tmp, &params);
        if (!type) {
            type = strdup(tmp);
        }
    } while (next);

    dm_task_destroy(task);
    task = NULL;
    return type;
}

char *
nashDmGetUUID(const char *name)
{
    char *uuid = NULL;
    struct dm_task *task;
    struct dm_info info;

    if (!nashDmGetInfo(name, &task, &info)) {
        uuid = (char *)dm_task_get_uuid(task);
        if (uuid) {
            if (uuid[0] == '\0')
                uuid = NULL;
            else
                uuid = strdup(uuid);
        }
        dm_task_destroy(task);
    }

    return uuid;
}

static dev_t
nashDmGetDev(const char *name)
{
    struct dm_task *task;
    struct dm_info info;
    dev_t ret = 0;

    task = dm_task_create(DM_DEVICE_INFO);
    if (!task)
        return 0;

    dm_task_set_name(task, name);
    dm_task_run(task);

    dm_task_get_info(task, &info);
    if (!info.exists) {
        if (task)
            dm_task_destroy(task);
        return 0;
    }

    ret = makedev(info.major, info.minor);
    dm_task_destroy(task);

    return ret;
}

static int
nashDmMapExists(const char *name)
{
    struct dm_task *task;
    struct dm_info info;

    if (nashDmGetInfo(name, &task, &info))
        return 0;
    dm_task_destroy(task);
    return 1;
}

int
nashDmCreate(char *name, char *uuid, long long start, long long length,
        char *type, char *params)
{
    struct dm_task *task;
    int rc;

    if (nashDmMapExists(name))
        return 1;

    task = dm_task_create(DM_DEVICE_CREATE);
    if (!task)
        return 0;

    dm_task_set_name(task, name);
    if (uuid)
        dm_task_set_uuid(task, uuid);

    dm_task_add_target(task, start, length, type, params);

    rc = dm_task_run(task);
    dm_task_destroy(task);

    dm_task_update_nodes();

    if (rc < 0)
        return 0;

    return 1;
}

int
nashDmRemove(char *name)
{
    struct dm_task *task;
    int rc;

    task = dm_task_create(DM_DEVICE_REMOVE);
    if (!task)
        return 0;

    dm_task_set_name(task, name);

    rc = dm_task_run(task);
    dm_task_destroy(task);

    dm_task_update_nodes();

    if (rc < 0)
        return 0;

    return 1;
}

static int nashPartedError = 0;
static int nashPartedErrorDisplay = 1;

static nashContext *nash_parted_context = NULL;

static PedExceptionOption
nashPartedExceptionHandler(PedException *ex)
{
    nash_log_level level;

    switch (ex->type) {
        case PED_EXCEPTION_INFORMATION:
            nashPartedError = 0;
            level = NASH_NOTICE;
            break;
        case PED_EXCEPTION_WARNING:
            nashPartedError = 0;
            level = NASH_WARNING;
            break;
        default:
            level = NASH_ERROR;
            nashPartedError = 1;
            break;
    }
    if (nashPartedErrorDisplay)
        nashLogger(nash_parted_context, level, "%s\n", ex->message);
    switch (ex->options) {
        case PED_EXCEPTION_OK:
        case PED_EXCEPTION_CANCEL:
        case PED_EXCEPTION_IGNORE:
            return ex->options;
        default:
            break;
    }
    return PED_EXCEPTION_UNHANDLED;
}

int
nashDmCreatePartitions(char *path)
{
    PedDevice *dev;
    PedDisk *disk;
    PedPartition *part = NULL;
    PedExceptionHandler *old_handler;
    char *namestart;
    int nparts = 0;
    struct stat sb;
    char *parent_uuid;
    char *newpath = NULL;

    if (path[0] != '/') {
        if (asprintf(&newpath, "/dev/mapper/%s", path) == -1)
            return nparts;
        path = newpath;
    }
    if (stat(path, &sb) < 0 || !S_ISBLK(sb.st_mode))
        return nparts;

    namestart = strrchr(path, '/');
    if (!namestart || !*(namestart++))
        goto out;
    if (!*namestart)
        goto out;

    parent_uuid = nashDmGetUUID(namestart);

    old_handler = ped_exception_get_handler();
    nash_parted_context = _nash_context;
    ped_exception_set_handler(nashPartedExceptionHandler);

    dev = ped_device_get(path);
    if (!dev || nashPartedError)
        goto out;

    if (!ped_device_open(dev))
        goto out;

    disk = ped_disk_new(dev);
    if (!disk || nashPartedError)
        goto out;

    part = ped_disk_next_partition(disk, NULL);
    while (part) {
        if (ped_partition_is_active(part))  {
            int i;
            char *name = NULL;
            char *table = NULL;
            char *uuid = NULL;

            i = asprintf(&name, "%sp%d", namestart, part->num);
            if (i < 0)
                continue;

            i = asprintf(&table, "%d:%d %Ld", major(sb.st_rdev),
                        minor(sb.st_rdev), part->geom.start);
            if (i < 0) {
                free(name);
                continue;
            }

            if (parent_uuid) {
                i = asprintf(&uuid, "%s-partition%d", parent_uuid, part->num);
                if (i < 0) {
                    free(name);
                    free(table);
                    continue;
                }
            }

            nparts += nashDmCreate(name, uuid, 0, part->geom.length,
                    "linear", table);

            free(uuid);
            free(table);
            free(name);
        }
        part = ped_disk_next_partition(disk, part);
    }

out:
    dm_task_update_nodes();

    if (disk) {
        ped_disk_destroy(disk);
        ped_device_close(dev);
    }

    if (dev)
        ped_device_destroy(dev);

    if (parent_uuid)
        free(parent_uuid);
    
    if (newpath)
        free(newpath);

    ped_exception_set_handler(old_handler);
    nashPartedError = 0;
    return nparts;
}

/* ok, this whole data structure pretty much just sucks ass. */
struct dm_iter_object {
    const char *name;
    dev_t devno;
    char *type;

    int position;
    int visited;

    struct dm_iter_object **deps;
    int ndeps;
};

static int
dm_iter_object_namesort(const void *ov0, const void *ov1)
{
    const struct dm_iter_object *o0 = *(void **)ov0, *o1 = *(void **)ov1;

    if (!o0 || !o1)
        return (o0 ? 1 : 0) - (o1 ? 1 : 0);
    return strcmp(o0->name, o1->name);
}

static int
dm_iter_object_devsort(const void *ov0, const void *ov1)
{
    const struct dm_iter_object *o0 = *(void **)ov0, *o1 = *(void **)ov1;
    return o1->devno - o0->devno;
}

struct dm_iter {
    union {
        struct dm_iter_object **objects;
        char **names;
    };
    const char **prune_names;
    size_t nobjs;
    int i;
};

static struct dm_iter *
dm_iter_begin(const char **names)
{
    struct dm_iter *iter;
    struct dm_names *dmnames;
    int i = 0, j = 0;
    unsigned int next = 0;
    struct dm_task *task;

    iter = calloc(1, sizeof (struct dm_iter));
    iter->names = calloc(i+1, sizeof (void *));

    if (names && *names)
        iter->prune_names = names;

    task = dm_task_create(DM_DEVICE_LIST);
    if (!task) {
        free(iter->names);
        free(iter);
        return NULL;
    }
    dm_task_run(task);
    dmnames = dm_task_get_names(task);
    do {
        dmnames = (void *)dmnames + next;

        iter->names = realloc(iter->names, sizeof (void *) * (i+1));
        iter->names[i++] = strdup(dmnames->name);

        next = dmnames->next;
    } while (next);
    iter->nobjs = i;

    dm_task_destroy(task);
    for (i = 0; i < iter->nobjs; i++) {
        struct dm_iter_object *obj = calloc(1, sizeof (struct dm_iter_object));

        obj->name = iter->names[i];
        obj->devno = nashDmGetDev(obj->name);
        obj->type = nashDmGetType(obj->name);
        obj->position = i;

        iter->objects[i] = obj;
    }
    qsort(iter->objects, i, sizeof (struct dm_iter_object *),
            dm_iter_object_devsort);

    for (i = 0; i < iter->nobjs; i++) {
        struct dm_iter_object *obj = iter->objects[i];
        struct dm_deps *deps;
        struct dm_task *task;

        nashDmTaskNew(DM_DEVICE_DEPS, obj->name, &task);

        deps = dm_task_get_deps(task);

        obj->deps = calloc(1, sizeof (struct dm_iter_object *));
        obj->deps[0] = NULL;
        obj->ndeps = deps->count;
        for (j = 0; j < obj->ndeps; j++) {
            struct dm_iter_object **depp;
            struct dm_iter_object dummy = {
                .devno = deps->device[j]
            };
            struct dm_iter_object *dummyp = &dummy;

            obj->deps = realloc(obj->deps,
                sizeof (struct dm_iter_object *) * (j+1));
            depp = bsearch(&dummyp, iter->objects, iter->nobjs,
                sizeof (struct dm_iter_object *), dm_iter_object_devsort);
            if (!depp) {
                obj->ndeps--;
                j--;
                continue;
            }
            obj->deps[j] = *depp;
        }
        qsort(obj->deps, obj->ndeps, sizeof (struct dm_iter_object *),
                dm_iter_object_devsort);
    }

    return iter;
}

static inline void
dm_iter_reset(struct dm_iter *iter)
{
    int i;

    for (i = 0; i < iter->nobjs; i++) {
        iter->objects[i]->visited = 0;
    }
}

static inline void
_dm_iter_destroy(struct dm_iter **iterp)
{
    int i;
    struct dm_iter *iter = *iterp;

    if (!iter)
        return;

    for (i = 0; i < iter->nobjs; i++) {
        struct dm_iter_object *obj;

        obj = iter->objects[i];
        free((char *)obj->name);
        free(obj->type);
        free(obj->deps);
        free(obj);
    }

    free(iter->names);
    free(iter);
    *iterp = NULL;
}

#define dm_iter_destroy(_i) _dm_iter_destroy(&(_i))

static inline struct dm_iter_object *
_dm_iter_next(struct dm_iter *iter, struct dm_iter_object *obj, int descend)
{
    int i;
    if (obj->visited)
        return NULL;
    if (obj->ndeps == 0) {
        obj->visited = 1;
        return obj;
    }
    if (descend) {
        for (i = 0; i < obj->ndeps; i++) {
            struct dm_iter_object *dep;

            dep = obj->deps[i];

            if (dep->visited)
                continue;
            return _dm_iter_next(iter, dep, 0);
        }
    }
    obj->visited = 1;
    return obj;
}

static struct dm_iter_object *
dm_iter_next(struct dm_iter *iter, int descend)
{
    int i, j;

    if (iter->prune_names) {
        for (i = 0; iter->prune_names[i]; i++) {
            struct dm_iter_object dummy = {
                .name = iter->prune_names[i],
            };
            struct dm_iter_object *dummyp = &dummy;
            struct dm_iter_object **objp, *obj;

            objp = lfind(&dummyp, iter->objects, &(iter->nobjs),
                    sizeof (struct dm_iter_object *), dm_iter_object_namesort);
            if (!objp)
                continue;
            obj = _dm_iter_next(iter, *objp, descend);
            if (obj)
                return obj;
        }
        return NULL;
    }
    for (i = 0; i < iter->nobjs; i++) {
        struct dm_iter_object *obj = iter->objects[i];
        int visit = 1;

        if (obj->visited)
            continue;
        if (obj->ndeps == 0) {
            obj->visited = 1;
            return obj;
        }
        if (descend) {
            for (j = 0; j < obj->ndeps; j++) {
                struct dm_iter_object *dep = obj->deps[j];

                if (!dep->visited) {
                    visit = 0;
                    break;
                }
            }
        }
        if (visit) {
            obj->visited = 1;
            return obj;
        }
    }
    return NULL;
}

static void
dm_print_rmparts(const char *name)
{
    static int major;
    struct dm_task *task;
    struct dm_deps *deps;
    int ret, i;

    if (!major)
        major = getDevNumFromProc("/proc/devices", "device-mapper");
    if (!major)
        return;

    ret = nashDmTaskNew(DM_DEVICE_DEPS, name, &task);
    if (ret < 0)
        return;

    deps = dm_task_get_deps(task);
    if (!deps) {
        dm_task_destroy(task);
        return;
    }

    for (i=0; i < deps->count; i++) {
        if (major(deps->device[i]) != major) {
            char *path = nashFindDeviceByDevno(_nash_context, deps->device[i]);
            if (path)
                printf("rmparts %s\n", path);
        }
    }
    dm_task_destroy(task);
}

static int
open_part(const char *name, PedDevice **dev, PedDisk **disk)
{
    int open = 0;
    char *path = NULL;
    PedExceptionHandler *old_handler;
    int display = nashPartedErrorDisplay;

    asprintf(&path, "/dev/mapper/%s", name);

    old_handler = ped_exception_get_handler();
    nash_parted_context = _nash_context;
    ped_exception_set_handler(nashPartedExceptionHandler);
    nashPartedErrorDisplay = 0;

    *dev = ped_device_get(path);
    free(path);
    if (!*dev || nashPartedError)
        goto out;

    if (!ped_device_open(*dev))
        goto out;
    open = 1;

    *disk = ped_disk_new(*dev);
    if (!*disk || nashPartedError)
        goto out;

    ped_exception_set_handler(old_handler);
    nashPartedError = 0;
    return 0;
out:
    if (*disk)
        ped_disk_destroy(*disk);
    *disk = NULL;
    if (open)
        ped_device_close(*dev);
    *dev = NULL;
    ped_exception_set_handler(old_handler);
    nashPartedErrorDisplay = display;
    nashPartedError = 0;
    return -1;
}

static int
dm_submap_has_part(const struct dm_iter_object const *parent, PedGeometry *geom)
{
    int nonlinear = 0;
    int haspart = 0;
    int display = nashPartedErrorDisplay;

    /* this is a bit of a hack */
    struct dm_tree *tree;
    struct dm_tree_node *pnode, *cnode;
    void *handle = NULL;
    struct dm_iter *iter;
    struct dm_iter_object *obj;

    tree = dm_tree_create();

    iter = dm_iter_begin(NULL);
    if (!iter)
        return 0;
    while ((obj = dm_iter_next(iter, 1)))
        dm_tree_add_dev(tree, major(obj->devno), minor(obj->devno));

    pnode = dm_tree_find_node(tree, major(parent->devno), minor(parent->devno));
    
    while ((cnode = dm_tree_next_child(&handle, pnode, 1))) {
        void *next = NULL;
        struct dm_task *task;
        int ret;

        //printf("testing %s\n", dm_tree_node_get_name(cnode));
        ret = nashDmTaskNew(DM_DEVICE_TABLE, dm_tree_node_get_name(cnode),
                &task);
        if (ret < 0)
            continue;

        do {
            u_int64_t start, length;
            char *type, *params;
            const char *name = NULL;
            PedDevice *dev = NULL;
            PedDisk *disk = NULL;
            PedPartition *part = NULL;

            next = dm_get_next_target(task, next, &start, &length,
                    &type, &params);
            if (!type || !params)
                continue;
            if (strcmp(type, "linear")) {
                nonlinear++;
                continue;
            }

            name = dm_tree_node_get_name(cnode);
            if (!name)
                continue;
            if (open_part(name, &dev, &disk) < 0)
                continue;

            part = ped_disk_next_partition(disk, NULL);
            if (part && !nashPartedError) {
                while (part)  {
                    part = ped_disk_next_partition(disk, part);
                    if (!part || nashPartedError)
                        continue;
                    if (!ped_partition_is_active(part))
                        continue;
                    if (geom->start == part->geom.start &&
                            geom->end == part->geom.end)
                        haspart = 1;
                }
            }

            if (disk) {
                ped_disk_destroy(disk);
                ped_device_close(dev);
            }
        } while (next);
    }
    dm_tree_free(tree);

    nashPartedErrorDisplay = display;
    nashPartedError = 0;

    if (nonlinear || haspart)
        return 1;
    return 0;
}

static int
dm_should_partition(const struct dm_iter_object const *obj)
{
    PedDevice *dev = NULL;
    PedDisk *disk = NULL;
    PedPartition *part = NULL;
    PedExceptionHandler *old_handler;
    int ret = 0;
    int display = nashPartedErrorDisplay;

    old_handler = ped_exception_get_handler();
    nash_parted_context = _nash_context;
    ped_exception_set_handler(nashPartedExceptionHandler);
    nashPartedErrorDisplay = 0;

    if (open_part(obj->name, &dev, &disk) < 0)
        goto out;

    part = ped_disk_next_partition(disk, NULL);
    if (!part || nashPartedError)
        goto out;

    ret = 1;
    while (part) {
        part = ped_disk_next_partition(disk, part);
        if (!part || nashPartedError)
            continue;
        if (!ped_partition_is_active(part))
            continue;
        if (dm_submap_has_part(obj, &part->geom))
            ret = 0;
    }
out:
    if (disk) {
        ped_disk_destroy(disk);
        ped_device_close(dev);
    }
    ped_exception_set_handler(old_handler);
    nashPartedErrorDisplay = display;
    nashPartedError = 0;
    return ret;
}

int
dm_list_sorted(const char **names)
{
    struct dm_iter *iter;
    struct dm_iter_object *obj;
    char **newnames;

    iter = dm_iter_begin(names);
    if (!iter)
        return 1;

    newnames = calloc(1, sizeof (char *));
    while ((obj = dm_iter_next(iter, 1))) {
        dm_print_rmparts(obj->name);
        printf("create %s\n", obj->name);
        if (dm_should_partition(obj))
            printf("part %s\n", obj->name);
    }

    dm_iter_destroy(iter);
    return 0;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
