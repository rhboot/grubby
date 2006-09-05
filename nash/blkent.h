/*
 * blkent.h - API for blktab entry management
 *
 * Peter Jones <pjones@redhat.com>
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

#ifndef BLKENT_H
#define BLKENT_H 1

#include <stdio.h>

struct blkent {
    char *blk_name;     /* symbolic block device name */
    char *blk_type;     /* rule type */
    char *blk_opts;     /* opts */
};

extern FILE *setblkent(const char *file, const char *mode);

extern struct blkent *getblkent(FILE *stream);

extern struct blkent *getblkent_r (FILE *stream,
                                   struct blkent *result,
                                   char *buffer,
                                   int bufsize);

extern int addblkent(FILE *stream, const struct blkent *mnt);

extern int endblkent(FILE *stream);

char *hasblkopt (const struct blkent *blk, const char *opt);
#endif /* defined(BLKENT_H) */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
