/*
 * wrap.c -- wrappers for libc functions.
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

#include <errno.h>
#include <unistd.h>

#include <nash.h>
#include "util.h"

extern int __real_open(const char *path, int flags, ...);
extern FILE *__real_fopen(const char *path, const char *mode);
extern DIR *__real_opendir(const char *name);
extern int __real_socket(int domain, int type, int protocol);
extern int __real_pipe(int filedes[2]);

int __wrap_open(const char *path, int flags, ...)
    __attribute__ ((weak, alias ("nash_wrap_open")));
FILE *__wrap_fopen(const char *path, const char *mode)
    __attribute__ ((weak, alias ("nash_wrap_fopen")));
DIR *__wrap_opendir(const char *name)
    __attribute__ ((weak, alias ("nash_wrap_opendir")));
int __wrap_socket(int domain, int type, int protocol)
    __attribute__ ((weak, alias ("nash_wrap_socket")));
int __wrap_pipe(int filedes[2])
    __attribute__ ((weak, alias ("nash_wrap_pipe")));

int
nash_wrap_open(const char *path, int flags, ...)
{
    int fd, rc;
    mode_t mode = 0;
    long errnum;

    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }

    fd = __real_open(path, flags, mode);
    if (fd < 0)
        return fd;

    rc = setFdCoe(fd, 1);
    if (rc < 0) {
        errnum = errno;
        close(fd);
        errno = errnum;
        return rc;
    }

    return fd;
}

FILE *
nash_wrap_fopen(const char *path, const char *mode)
{
    FILE *f;
    int rc;
    long errnum;

    f = __real_fopen(path, mode);
    if (!f)
        return f;

    rc = setFdCoe(fileno(f), 1);
    if (rc < 0) {
        errnum = errno;
        fclose(f);
        errno = errnum;
        return NULL;
    }

    return f;
}

DIR *
nash_wrap_opendir(const char *name)
{
    DIR *d;
    int rc;
    long errnum;

    d = __real_opendir(name);
    if (!d)
        return d;

    rc = setFdCoe(dirfd(d), 1);
    if (rc < 0) {
        errnum = errno;
        closedir(d);
        errno = errnum;
        return NULL;
    }

    return d;
}

int
nash_wrap_socket(int domain, int type, int protocol)
{
    int fd;
    int rc;
    int errnum;

    fd = __real_socket(domain, type, protocol);
    if (fd < 0)
        return fd;

    rc = setFdCoe(fd, 1);
    if (rc < 0) {
        errnum = errno;
        close(fd);
        errno = errnum;
        return rc;
    }

    return fd;
}

int
nash_wrap_pipe(int filedes[2])
{
    int rc;
    int x;
    int fds[2];

    rc = __real_pipe(fds);
    if (rc < 0)
        return rc;

    for (x = 0; x < 2; x++) {
        int status;
        int errnum;

        status = setFdCoe(fds[x], 1);
        if (status < 0) {
            errnum = errno;
            close(fds[0]);
            close(fds[1]);
            errno = errnum;
            return status;
        }
    }
    filedes[0] = fds[0];
    filedes[1] = fds[1];

    return rc;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
