/*
 * module.c - module interface for a block device probe
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2005,2006 Red Hat, Inc.
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
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <argz.h>
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

    return 0;
}

/* end probe registration */
/* begin loading and unloading */

static void bdevid_probe_unload_cb(gpointer data, gpointer user_data)
{
    struct bdevid_module *m = user_data;
    g_ptr_array_remove(m->probes, data);
    free(data);
}

int bdevid_module_unload(struct bdevid *b, char *name)
{
    struct bdevid_module *mod = NULL;

    mod = g_hash_table_lookup(b->modules, (gconstpointer)name);
    if (mod) {
        if (mod->probes) {
            g_ptr_array_foreach(mod->probes, bdevid_probe_unload_cb, mod);
            g_ptr_array_free(mod->probes, TRUE);
        }

        if (mod->bdevid)
            g_hash_table_remove(b->modules, (gconstpointer)mod->name);

        if (mod->dlh)
            dlclose(mod->dlh);

        free(mod);
    }

    return 0;
}

int bdevid_module_load_file_maybe(struct bdevid *b, char *file, char *name)
{
    struct bdevid_module *m;
    struct bdevid_module_ops **ops;
    int rc = -1;

    if (access(file, X_OK))
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
    if (name && strcmp(m->name, name))
        goto out;

    if (g_hash_table_lookup(b->modules, (gconstpointer)m->name)) {
        rc = 0;
        goto out;
    }

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
    if (!rc)
        errno = 0;
    return rc;
}

int bdevid_module_load_file(struct bdevid *b, char *file)
{
    return bdevid_module_load_file_maybe(b, file, NULL);
    
}

int bdevid_module_load(struct bdevid *b, char *name)
{
    char *dir;

    if (g_hash_table_lookup(b->modules, (gconstpointer)name))
        return 0;

    for (dir = b->module_pathz; dir;
            dir = argz_next(b->module_pathz, b->module_pathz_len, dir)) {
        char *file = NULL;

        if (asprintf(&file, "%s/%s.so", dir, name) < 0)
            continue;

        if (bdevid_module_load_file_maybe(b, file, name) < 0) {
            free(file);
            continue;
        }
        free(file);
        return 0;
    }

    return -1;
}

int bdevid_module_load_all(struct bdevid *b)
{
    char *cur;

    for (cur = b->module_pathz; cur; 
            cur = argz_next(b->module_pathz, b->module_pathz_len, cur)) {
        DIR *dir = NULL;
        struct dirent *dent = NULL;

        if (!(dir = opendir(cur)))
            continue;

        while ((dent = readdir(dir))) {
            char *file = NULL;
            if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
                continue;

            if (asprintf(&file, "%s/%s", cur, dent->d_name) < 0)
                continue;

            bdevid_module_load_file(b, file);

            free(file);
        }
        closedir(dir);
    }

    return 0;
}

static gboolean
bdevid_module_unload_cb(gpointer key, gpointer value, gpointer priv)
{
    struct bdevid_module *mod = (struct bdevid_module *)value;
    struct bdevid *b = (struct bdevid *)priv;

    if (mod->probes) {
        g_ptr_array_foreach(mod->probes, bdevid_probe_unload_cb, mod);
        g_ptr_array_free(mod->probes, TRUE);
    }

    if (mod->bdevid)
        g_hash_table_remove(b->modules, (gconstpointer)mod->name);

    if (mod->dlh)
        dlclose(mod->dlh);

    free(mod);

    return TRUE;
}

int bdevid_module_unload_all(struct bdevid *b)
{
    g_hash_table_foreach_remove(b->modules, bdevid_module_unload_cb, b);
    return 0;
}

/* end loading and unloading */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
