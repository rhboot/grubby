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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

extern nashContext *_nash_context;

static int
getDevNumFromProc(char * file, char * device)
{
    char buf[32768];
    char * start, *end;
    int num;
    int fd;

    if ((fd = open(file, O_RDONLY)) == -1) {
        return -1;
    }

    num = read(fd, buf, sizeof(buf));
    if (num < 1) {
        close(fd);
        return -1;
    }
    buf[num] = '\0';
    close(fd);

    start = buf;
    end = strchr(start, '\n');
    while (start && end) {
        int off;
        *end++ = '\0';
        if (sscanf(start, "%d %n", &num, &off) &&
                strncmp(device, start + off, strlen(device)) == 0)
            return num;
        start = end;
        end = strchr(start, '\n');
    }
    return -1;
}

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

static int __attribute__((used))
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
        if (!*end)
            break;

        *end = '\0';
        if (access(buf, F_OK) && errno == ENOENT)
            mkdir(buf, 0755);
        *end = '/';
        end++;
    } while (1);

    return mknod(buf, mode, dev);
}

extern int getDevNumFromProc(char * file, char * device);

extern int stringsort(const void *v0, const void *v1);

static inline void
udelayspec(struct timespec rem)
{
    while (nanosleep(&rem, &rem) == -1 && errno == EINTR)
        ;
}

static inline void
udelay(long long usecs)
{
    struct timespec rem = {0,0};

    rem.tv_sec = usecs / 1000000;
    rem.tv_nsec = (usecs % 1000000) * 1000;
    udelayspec(rem);
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

#include <sys/syscall.h>
#include <sys/poll.h>
#ifdef __ia64__
#define nash_ppoll(fds, nfds, timeout, sigmask, nsigs) \
        ppoll(fds, nfds, timeout, sigmask)
#else
#define nash_ppoll(fds, nfds, timeout, sigmask, nsigs) \
        syscall(SYS_ppoll, fds, nfds, timeout, sigmask, nsigs)
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
