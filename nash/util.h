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

#ifndef NASH_PRIV_UTIL_H
#define NASH_PRIV_UTIL_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static int __attribute__((used))
setFdCoe(int fd, int enable)
{
    int rc;
    long flags = 0;

    rc = fcntl(fd, F_GETFD, &flags);
    if (rc < 0)
        return rc;

    if (enable)
        flags |= FD_CLOEXEC;
    else
        flags &= ~FD_CLOEXEC;

    rc = fcntl(fd, F_SETFD, flags);
    return rc;
}

static int __attribute__((used))
readFD (int fd, char **buf)
{
    char *p;
    size_t size = 16384;
    int s, filesize;

    *buf = calloc (16384, sizeof (char));
    if (*buf == 0)
        return -1;

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

extern int smartmknod(const char * device, mode_t mode, dev_t dev);

extern int getDevNumFromProc(char * file, char * device);

extern int stringsort(const void *v0, const void *v1);

static void __attribute__((used))
udelay(long long usecs)
{
    struct timespec req = {
        .tv_sec = 0,
        .tv_nsec = 0,
    };
    struct timespec rem = {
        .tv_sec = 0,
        .tv_nsec = 0,
    };
    struct timespec *reqp = &req, *remp = &rem;

    while (usecs >= 100000) {
        req.tv_sec++;
        usecs -= 100000;
    }
    req.tv_nsec = usecs * 100;
     
    while(nanosleep(reqp, remp) == -1 && errno == EINTR) {
        reqp = remp;
        remp = reqp == &req ? &rem : &req;
        errno = 0;
    }
}

extern char *readlink_malloc(const char *filename);

#define readlinka(_filename) ({             \
        char *_buf0, *_buf1 = NULL;         \
        _buf0 = readlink_malloc(_filename); \
        if (_buf0 != NULL) {                \
            _buf1 = strdupa(_buf0);         \
            free(_buf0);                    \
        }                                   \
        _buf0 = _buf1;                      \
    })

#define asprintfa(str, fmt, ...) ({                 \
        char *_tmp = NULL;                          \
        int _rc;                                    \
        _rc = asprintf((str), (fmt), __VA_ARGS__);  \
        if (_rc != -1) {                            \
            _tmp = strdupa(*(str));                 \
            if (!_tmp) {                            \
                _rc = -1;                           \
            } else {                                \
                free(*(str));                       \
                *(str) = _tmp;                      \
            }                                       \
        }                                           \
        _rc;                                        \
    })


extern nashContext *_nash_context;

#if 0 /* not just yet */
extern int qprintf(const char *format, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((deprecated));
extern int eprintf(const char *format, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((deprecated));
#else
extern int qprintf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
extern int eprintf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
#endif

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_PRIV_UTIL_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
