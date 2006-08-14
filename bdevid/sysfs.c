/*
 * sysfs.c - utility functions for finding things from sysfs
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
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#include <nash.h>

#include "bdevid.h"
#include "util.h"

struct bdevid_sysfs_node {
    char *dir;
};

struct bdevid_sysfs_node *bdevid_sysfs_find_node(char *file)
{
    struct stat sb;
    char *sysfs_dir;
    struct bdevid_sysfs_node *node;

    if (stat(file, &sb) < 0)
        return NULL;

    if (!(sysfs_dir = nashFindDeviceByDevno(NULL, sb.st_rdev)))
        return NULL;

    if (!(node = calloc(1, sizeof(*node)))) {
        free(sysfs_dir);
        return NULL;
    }

    node->dir = sysfs_dir;
    return node;
}

char *bdevid_sysfs_get_name(struct bdevid_sysfs_node *node)
{
    /* XXX wrong wrong wrong */
    return node->dir;
}
char *bdevid_sysfs_get_dir(struct bdevid_sysfs_node *node)
{
    /* XXX sucky sucky */
    return node->dir;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
