/*
 * lib.c -- a small library for various parts of nash
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <argz.h>
#include <nash.h>
#include "lib.h"

int
nashDefaultLoggerV(nashContext *nc, const nash_log_level level,
    const char *format, va_list ap)
{
    FILE *output;
    va_list apc;
    int ret;

    switch (level) {
        case NASH_NOTICE:
            output = stdout;
            if (nc->quiet)
                return 0;
            break;
        case NASH_WARNING:
            output = stderr;
            if (nc->quiet)
                return 0;
            break;
        case NASH_ERROR:
        default:
            output = stderr;
            break;
    }

    va_copy(apc, ap);
    ret = vfprintf(output, format, apc);
    va_end(apc);

    fflush(output);
    return ret;
}

void
nashSetLogger(nashContext *nc, nashLogger_t logger)
{
    nc->logger = logger;    
}

nashLogger_t nashGetLogger(nashContext *nc)
{
    return nc->logger;
}

nashContext *
nashNewContext(void) {
    nashContext *nc;
    char *tmp;
    
    nc = calloc(1, sizeof(*nc));

    nc->logger = nashDefaultLoggerV;

    if ((tmp = strdup("/lib/firmware/")) == NULL) {
        free(nc);
        return NULL;
    }
    if (argz_create_sep(tmp, ':', &nc->fw_pathz, &nc->fw_pathz_len) != 0) {
        free(tmp);
        free(nc);
        return NULL;
    }
    nc->hp_childfd = -1;
    nc->hp_parentfd = -1;

    nashSetFileFetcher(nc, NULL);

    return nc;
}

void
_nashFreeContext(nashContext **nc)
{
    if (nc && *nc) {
        if ((*nc)->fw_pathz)
            free((*nc)->fw_pathz);
        free(*nc);
        *nc = NULL;
    }
}

int
nashLogger(nashContext *ctx, const nash_log_level level,
    const char *format, ...)
{
    va_list ap;
    int ret = 0;

    if (ctx->logger) {
        va_start(ap, format);
        ret = ctx->logger(ctx, level, format, ap);
        va_end(ap);
    }

    return ret;
}


#if 0
static inline int
init_hotplug_stub(void)
{
    return -1;
}
int init_hotplug(void)
    __attribute__ ((weak, alias ("init_hotplug_stub")));

static inline void
kill_hotplug_stub(void)
{
    ;
}
void kill_hotplug(void)
    __attribute__ ((weak, alias ("kill_hotplug_stub")));

static inline void
move_hotplug_stub(void)
{
    ;
}
void move_hotplug(void)
    __attribute__ ((weak, alias ("move_hotplug_stub")));

static inline void
notify_hotplug_of_exit_stub(void)
{
    ;
}
void notify_hotplug_of_exit(void)
    __attribute__ ((weak, alias ("notify_hotplug_of_exit_stub")));
#endif

/*
 * vim:ts=8:sw=4:sts=4:et
 */
