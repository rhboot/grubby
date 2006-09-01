/*
 * dm.h - backend library for partition table scanning on dm devices
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

#ifndef NASH_PRIV_DM_H
#define NASH_PRIV_DM_H 1

extern char *
nashDmGetUUID(const char *name);

extern int
nashDmCreate(char *name, char *uuid, long long start, long long length,
        char *type, char *params);

extern int
nashDmRemove(char *name);

extern int
nashDmCreatePartitions(nashContext *nc, char *path);

#if 0 /* notyet */
extern int
nashDmRemovePartitions(char *name);
#endif

extern int
nashDmListSorted(nashContext *nc, const char **names);

extern int
nashDmHasParts(const char *name);

extern void
nashDmFreeTree(void);

extern char *
nashDmGetDevName(dev_t devno);

#endif /* NASH_PRIV_DM_H */
