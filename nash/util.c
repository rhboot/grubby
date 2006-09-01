/*
 * util.c -- a small collection of utility functions for nash and libnash.a
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

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <nash.h>
#include "lib.h"
#include "util.h"

int
stringsort(const void *v0, const void *v1)
{
    const char * const *s0=v0, * const *s1=v1;
    return strcoll(*s0, *s1);
}


char *
readlink_malloc(const char *filename)
{
    int size=100;
    char *buffer = NULL;

    while (1) {
        int nchars;

        buffer = (char *)realloc(buffer, size);
        memset(buffer, '\0', size);
        if (!buffer)
            return NULL;
        nchars = readlink(filename, buffer, size-1);
        if (nchars < 0) {
            free(buffer);
            return NULL;
        }
        if (nchars < size-1)
            return buffer;
        size *= 2;
    }
}

nashContext *_nash_context = NULL;

int
qprintf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = _nash_context->logger(_nash_context, NASH_NOTICE, format, ap);
    va_end(ap);

    return ret;
}

int
eprintf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = _nash_context->logger(_nash_context, NASH_ERROR, format, ap);
    va_end(ap);

    return ret;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
