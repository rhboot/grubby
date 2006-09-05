/*
 * bdevid.c -- gives access to a bdevid from nash
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

#define _GNU_SOURCE 1

#include <bdevid.h>

#include <nash.h>
#include "lib.h"

struct bdevid *
nashBdevidInit(nashContext *nc) {
    if (nc->bdevid)
        return nc->bdevid;

    if (!(nc->bdevid = bdevid_new(NULL)))
        return NULL;
    nc->nashBdevidFinish = nashBdevidFinish;

    bdevid_module_load_all(nc->bdevid);

    return nc->bdevid;
}

void
nashBdevidFinish(nashContext *nc) {
    if (nc->bdevid) {
        bdevid_destroy(nc->bdevid);
        nc->bdevid = NULL;
    }
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
