/*
 * lib.h -- a small library for various parts of nash
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef NASH_PRIV_LIB_H
#define NASH_PRIV_LIB_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <nash.h>
#include <signal.h>

struct nashContext_s {
    nashLogger_t logger;
    int testing;
    int quiet;
    int reallyquiet;

    nashFileFetcher_t fetcher;
    char *fw_pathz;
    size_t fw_pathz_len;
    int hp_childfd;
    int hp_parentfd;
};

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_PRIV_LIB_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
