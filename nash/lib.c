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
 *
 * vim:ts=8:sw=4:sts=4:et
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"

static int
makeFdCoe(int fd)
{
    int rc;
    long flags;

    rc = fcntl(fd, F_GETFD, &flags);
    if (rc < 0)
        return rc;

    flags |= FD_CLOEXEC;

    rc = fcntl(fd, F_SETFD, &flags);
    return rc;
}

int
coeOpen(const char *path, int flags, ...)
{
    int fd, rc, mode = 0;
    long errnum;

    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, int);
        va_end(arg);
    }

    fd = open(path, flags);
    if (fd < 0)
        return fd;

    rc = makeFdCoe(fd);
    if (rc < 0) {
        errnum = errno;
        close(fd);
        errno = errnum;
        return rc;
    }

    return fd;
}

FILE *
coeFopen(const char *path, const char *mode)
{
    FILE *f;
    int rc;
    long errnum;

    f = fopen(path, mode);
    if (!f)
        return f;

    rc = makeFdCoe(fileno(f));
    if (rc < 0) {
        errnum = errno;
        fclose(f);
        errno = errnum;
        return NULL;
    }

    return f;
}

DIR *
coeOpendir(const char *name)
{
    DIR *d;
    int rc;
    long errnum;

    d = opendir(name);
    if (!d)
        return d;

    rc = makeFdCoe(dirfd(d));
    if (rc < 0) {
        errnum = errno;
        closedir(d);
        errno = errnum;
        return NULL;
    }

    return d;
}

int
readFD (int fd, char **buf)
{
    char *p;
    size_t size = 16384;
    int s, filesize;

    *buf = calloc (16384, sizeof (char));
    if (*buf == 0) {
        eprintf("calloc failed: %s\n", strerror(errno));
        return -1;
    }

    filesize = 0;
    do {
        p = &(*buf) [filesize];
        s = read (fd, p, 16384);
        if (s < 0)
            break;
        filesize += s;
        /* only exit for empty reads */
        if (s == 0)
            break;
        size += s;
        *buf = realloc (*buf, size);
    } while (1);

    if (filesize == 0 && s < 0) {
        free (*buf);
        *buf = NULL;
        return -1;
    }

    return filesize;
}

int testing = 0, quiet = 0, reallyquiet = 0;

int nashDefaultLogger(nash_log_level level, char *format, ...)
{
    int ret;
    va_list ap;

    va_start(ap, format);
    ret = vprintf(format, ap);
    va_end(ap);

    fflush(stdout);
    return ret;
}

int
nashLoggerV(const nash_log_level level, const char *format, va_list ap)
{
    FILE *output;
    va_list apc;
    int ret;

    switch (level) {
        case NOTICE:
            output = stdout;
            if (quiet)
                return 0;
            break;
        case WARNING:
            output = stderr;
            if (quiet)
                return 0;
            break;
        case ERROR:
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

int
nashLogger(const nash_log_level level, const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = nashLoggerV(level, format, ap);
    va_end(ap);

    return ret;
}


int
qprintf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = nashLoggerV(NOTICE, format, ap);
    va_end(ap);

    return ret;
}

int
eprintf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = nashLoggerV(ERROR, format, ap);
    va_end(ap);

    return ret;
}

int
smartmknod(char * device, mode_t mode, dev_t dev)
{
    char buf[256];
    char * end;

    strncpy(buf, device, 256);

    end = buf;
    while (*end) {
        if (*end == '/') {
            *end = '\0';
            if (access(buf, F_OK) && errno == ENOENT)
                mkdir(buf, 0755);
            *end = '/';
        }

        end++;
    }

    return mknod(device, mode, dev);
}
