/*
 * log.h -- logging functions for nash
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
#ifndef NASH_LOG_H
#define NASH_LOG_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <stdarg.h>

#include "lib.h"

typedef enum {
    NOTICE,
    WARNING,
    ERROR,
} nash_log_level;

typedef int (*nashLogger_t)(struct nash_context *, const nash_log_level,
    const char *, va_list)
    __attribute__((format(printf, 3, 0)));

extern void nashSetLogger(struct nash_context *, nashLogger_t);
extern nashLogger_t nashGetLogger(struct nash_context *);

extern int nashLogger(struct nash_context *, const nash_log_level level,
    const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_LOG_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
