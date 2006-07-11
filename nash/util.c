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
smartmknod(const char * device, mode_t mode, dev_t dev)
{
    char buf[PATH_MAX];
    char * end;

    strncpy(buf, device, 256);

    end = buf;
    do {
        size_t len;
        len = strcspn(end, "/!");
        end += len;
        if (!end || !*end)
            break;

        *end = '\0';
        if (access(buf, F_OK) && errno == ENOENT)
            mkdir(buf, 0755);
        *end = '/';
        end++;
    } while (1);

    return mknod(buf, mode, dev);
}

int
getDevNumFromProc(char * file, char * device)
{
    char buf[32768], line[4096];
    char * start, *end;
    int num;
    int fd;

    if ((fd = open(file, O_RDONLY)) == -1) {
        eprintf("can't open file %s: %s\n", file, strerror(errno));
        return -1;
    }

    num = read(fd, buf, sizeof(buf));
    if (num < 1) {
        close(fd);
        eprintf("failed to read %s: %s\n", file, strerror(errno));
        return -1;
    }
    buf[num] = '\0';
    close(fd);

    start = buf;
    end = strchr(start, '\n');
    while (start && end) {
        *end++ = '\0';
        if ((sscanf(start, "%d %s", &num, line)) == 2) {
            if (!strncmp(device, line, strlen(device)))
                return num;
        }
        start = end;
        end = strchr(start, '\n');
    }
    return -1;
}

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
