/*
 * bdevid.c - library interface for programs wanting to do block device
 *            probing
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
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <argz.h>
#include <glib.h>

#include "bdevid.h"

int bdevid_path_set(struct bdevid *b, char *path)
{
    char *old = b->module_pathz, *new = NULL;
    size_t old_len = b->module_pathz_len;
    int en;

    if (!path) {
        while (b->module_pathz)
            argz_delete(&b->module_pathz, &b->module_pathz_len,b->module_pathz);
        return 0;
    }
    b->module_pathz = NULL;
    b->module_pathz_len = 0;

    if (strlen(path) > 1023) {
        errno = E2BIG;
        goto out;
    }

    if ((new = strdup(path)) == NULL)
        goto out;

    if (argz_create_sep(new, ':', &b->module_pathz, &b->module_pathz_len) != 0)
        goto out;
    free(new);

    while (old)
        argz_delete(&old, &old_len, old);

    return 0;
out:
    en = errno;
    if (new)
        free(new);
    errno = en;
    b->module_pathz = old;
    b->module_pathz_len = old_len;

    return -1;
}

char *bdevid_path_get(struct bdevid *b)
{
    static char path[1024];
    char *pathz = NULL;
    size_t n;

    argz_stringify(b->module_pathz, b->module_pathz_len, ':');
    n = strlen(b->module_pathz);
    memmove(path, b->module_pathz, n > 1023 ? 1023 : n);
    path[1023] = '\0';

    n = 0;
    argz_create_sep(b->module_pathz, ':', &pathz, &n);
    b->module_pathz = pathz;
    b->module_pathz_len = n;

    return path;
}

void bdevid_destroy(struct bdevid *b)
{
    if (b) {
        if (b->modules) {
#if 0
        while (b->nmodules) {
            /* XXX fix this up */
            struct bdevid_module *bm = b->modules[0];
            struct modloader_module *mm = bm->modloader_module;

            bdevid_module_remove(bm);
            bdevid_module_dest(mm);
        }
#endif
            g_hash_table_destroy(b->modules);
        }

        while (b->module_pathz)
            argz_delete(&b->module_pathz, &b->module_pathz_len,b->module_pathz);
        memset(b, '\0', sizeof (*b));
        free(b);
    }
}

struct bdevid *bdevid_new(char *env)
{
    struct bdevid *b = NULL;
    char *env_path = NULL;

    if (!(b = calloc(1, sizeof (*b))))
        goto err;

    if (env)
        env_path = getenv(env);

    if (env_path) {
        bdevid_path_set(b, env_path);
    } else {
        bdevid_path_set(b, BDEVID_DEFAULT_SEARCH_PATH);
    }

    if (!(b->modules = g_hash_table_new(g_str_hash, g_str_equal)))
        goto err;

    return b;
err:
    bdevid_destroy(b);
    return NULL;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
