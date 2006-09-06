/*
 * bdevid.c - library interface for programs wanting to do block device
 *            probing
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
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#include "bdevid.h"
#include "priv.h"

struct bdevid_probe_result {
    struct bdevid_module *m;
    struct bdevid_probe *p;

    char *vendor;
    char *model;
    char *unique_id;
};

const char *bdevid_pr_getattr(struct bdevid_probe_result *r, const char *key)
{
    static char buf[1024] = {0};
    char *filter = " \n";
    char *s, *c;
    size_t n;

    if (!strcasecmp(key, "module"))
        strncpy(buf, r->m->name, 1023);
    else if (!strcasecmp(key, "probe"))
        strncpy(buf, r->p->name, 1023);
    else if (!strcasecmp(key, "vendor") && r->vendor)
        strncpy(buf, r->vendor, 1023);
    else if (!strcasecmp(key, "model") && r->model)
        strncpy(buf, r->model, 1023);
    else if (!strcasecmp(key, "unique_id") && r->unique_id)
        strncpy(buf, r->unique_id, 1023);
    else
        return NULL;

    n = strlen(buf) -1;
    for (s = buf + n; n; s = buf + --n) {
        int matched = 0;
        for (c = filter; *c; c++) {
            if (*s == *c) {
                matched = 1;
                *s = '\0';
                break;
            }
        }
        if (!matched)
            break;
    }
    
    if ((n = strspn(buf, filter)) > 0)
        memmove(buf, buf+n, strlen(buf+n));
    if (!*buf)
        return NULL;
    return buf;
}

struct bdevid_probe_data {
    struct bdevid *bdevid;
    struct bdevid_module *module;
    int proceed;

    struct bdevid_bdev *bdev;
    bdevid_probe_cb cb;
    void *priv;
};

static void bdevid_probe_probe_cb(gpointer data, gpointer priv)
{
    struct bdevid_probe *probe = data;
    struct bdevid_probe_data *pd = priv;
    struct bdevid_probe_result result = {
        .m = pd->module,
        .p = probe,
    };

    if (!pd->proceed)
        return;

    probe->ops->get_vendor(pd->bdev, &result.vendor);
    probe->ops->get_model(pd->bdev, &result.model);
    probe->ops->get_unique_id(pd->bdev, &result.unique_id);

    if (result.vendor || result.model || result.unique_id) {
        if (pd->cb(&result, pd->priv) < 0)
            pd->proceed = 0;
    }
    if (result.vendor)
        free(result.vendor);
    if (result.model)
        free(result.model);
    if (result.unique_id)
        free(result.unique_id);
    memset(&result, '\0', sizeof (result));
}

static void bdevid_probe_module_cb(gpointer key, gpointer value, gpointer priv)
{
    struct bdevid_probe_data *pd = priv;

    if (!pd->proceed)
        return;

    pd->module = value;
    g_ptr_array_foreach(pd->module->probes, bdevid_probe_probe_cb, pd);
}

int bdevid_probe(struct bdevid *b, char *file, bdevid_probe_cb cb, void *priv)
{
    struct bdevid_bdev bdev = {
        .file = file,
        .fd = -1,
    };
    struct bdevid_probe_data pd = {
        .bdevid = b,
        .module = NULL,
        .proceed = 1,
        .bdev = &bdev,
        .cb = cb,
        .priv = priv,
    };

    struct bdevid_sysfs_node *node = NULL;
    
    if (!(node = bdevid_sysfs_find_node(file)))
        return -1;

    bdev.name = bdevid_sysfs_get_name(node);
    bdev.sysfs_dir = bdevid_sysfs_get_dir(node);

    if ((bdev.fd = open(file, O_RDWR|O_NONBLOCK)) < 0) {
        bdevid_sysfs_free_node(node);
        return bdev.fd;
    }

    g_hash_table_foreach(b->modules, bdevid_probe_module_cb, &pd);
    bdevid_sysfs_free_node(node);
    return 1;
}
/*
 * vim:ts=8:sw=4:sts=4:et
 */
