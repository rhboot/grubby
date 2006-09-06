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


struct bdevid_bdev {
    char *file;
    int fd;
    char *name;
    char *sysfs_dir;
};


/*
 * vim:ts=8:sw=4:sts=4:et
 */
