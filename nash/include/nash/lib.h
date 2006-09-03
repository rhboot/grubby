/*
 * lib.h -- a small library for various parts of nash
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

#ifndef NASH_LIB_H
#define NASH_LIB_H 1

typedef struct nashContext_s nashContext;

extern nashContext *nashNewContext(void);
extern void _nashFreeContext(nashContext **);
#define nashFreeContext(x) _nashFreeContext(&(x))

extern int nashBdevidInit(nashContext *nc);
extern void nashBdevidFinish(nashContext *nc);

#endif /* NASH_LIB_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
