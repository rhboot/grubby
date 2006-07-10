/*
 * module.c - module interface for a block device probe
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2005,2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define _GNU_SOURCE 1

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <glib.h>

#include "bdevid.h"

/* probe registration */

int bdevid_register_probe(struct bdevid_module *m, struct bdevid_probe_ops *ops)
{
    struct bdevid_probe *probe;
    
    if (!(probe = calloc(1, sizeof (*probe))))
        return -1;

    g_ptr_array_add(m->probes, probe);
    probe->module = m;
    probe->ops = ops;
    probe->name = probe->ops->name;

    printf("module <%p> registered probe <%p>\n", m, probe);
    return 0;
}

/* end probe registration */

struct bdevid_device_probe_data {
    struct bdevid *bdevid;
    struct bdevid_module *module;
    int proceed;

    char *file;
    int fd;
    bdevid_device_probe_visitor visitor;
    void *user_data;
};

static void bdevid_device_probe_probe_helper(gpointer data, gpointer user_data)
{
    struct bdevid_probe *probe = data;
    struct bdevid_device_probe_data *udata = user_data;
    char *id = NULL;

    if (!udata->proceed)
        return;

    if (probe->ops->probe(udata->fd, &id) >= 0 && id) {
        int rc;

        rc = udata->visitor(udata->module->name, probe->name, id);
        free(id);
        if (rc < 0)
            udata->proceed = 0;
    }
}

static void bdevid_device_probe_module_helper(gpointer key, gpointer value,
    gpointer user_data)
{
    struct bdevid_device_probe_data *udata = user_data;

    if (!udata->proceed)
        return;

    udata->module = value;
    g_ptr_array_foreach(udata->module->probes,
                        bdevid_device_probe_probe_helper, user_data);
}

int bdevid_probe_device(struct bdevid *b, char *file,
    bdevid_device_probe_visitor visitor, void *user_data)
{
    struct bdevid_device_probe_data udata = {
        .bdevid = b,
        .module = NULL,
        .proceed = 1,
        .file = file,
        .fd = -1,
        .visitor = visitor,
        .user_data = user_data,
    };

    if ((udata.fd = open(file, O_EXCL)) < 0)
        return udata.fd;

    g_hash_table_foreach(b->modules, bdevid_device_probe_module_helper,
        &udata);
    return 1;
}

/* begin loading and unloading */

int bdevid_module_unload(struct bdevid_module *m)
{
    /* XXX write me */
    return 0;
}

int bdevid_module_load(struct bdevid *b, char *file)
{
    struct bdevid_module *m;
    struct bdevid_module_ops **ops;

    if (access(file, R_OK|X_OK))
        return -1;

    if (!(m = calloc(1, sizeof (*m))))
        return -1;

    if (!(m->dlh = dlopen(file, RTLD_LAZY|RTLD_LOCAL)))
        goto out;

    if (!(ops = dlsym(m->dlh, "bdevid_module_ops")) || !*ops)
        goto out;

    m->ops = *ops;
    if (m->ops->magic != BDEVID_MAGIC)
        goto out;

    m->name = m->ops->name;

    if (g_hash_table_lookup(b->modules, (gconstpointer)m->name))
        goto out;

    g_hash_table_insert(b->modules, (gpointer)m->name, (gpointer)m);
    m->bdevid = b;

    if (!(m->probes = g_ptr_array_new()))
        goto out;

    if (m->ops->init(m) < 0)
        goto out;

    return 0;
out:
    if (m) {
        int en = errno;

        if (m->bdevid)
            g_hash_table_remove(b->modules, (gconstpointer)m->name);

        if (m->dlh)
            dlclose(m->dlh);

        memset(m, '\0', sizeof (*m));
        free(m);
        errno = en;
    }
    m = NULL;
    return -1;
}

int bdevid_module_load_all(struct bdevid *b)
{
    /* XXX write me */
    return 0;
}

int bdevid_module_unload_all(struct bdevid *b)
{
    /* XXX write me */
    return 0;
}

/* end loading and unloading */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
