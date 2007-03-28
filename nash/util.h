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
#include <sys/time.h>
#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

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

#define USECS_PER_SEC 1000000LL
#define NSECS_PER_USEC 1000LL
#define NSECS_PER_SEC (NSECS_PER_USEC * USECS_PER_SEC)

static inline void
nsectospec(long long nsecs, struct timespec *ts)
{
    if (nsecs < 0) {
        ts->tv_sec = -1;
        ts->tv_nsec = -1;
        return;
    }
    ts->tv_sec = nsecs / NSECS_PER_SEC;
    ts->tv_nsec = (nsecs % NSECS_PER_SEC);
}

static inline void
usectospec(long long usecs, struct timespec *ts)
{
    if (usecs > 0 && LLONG_MAX / NSECS_PER_USEC > usecs)
        usecs *= NSECS_PER_USEC;
    
    nsectospec(usecs, ts);
}

static inline int
specinf(struct timespec *ts)
{
    if (ts->tv_sec < 0 || ts->tv_nsec < 0)
        return 1;
    return 0;
}

static inline long long
spectonsec(struct timespec *ts)
{
    long long nsecs = 0;
    if (specinf(ts))
        return -1;
    
    nsecs = ts->tv_sec * NSECS_PER_SEC;
    nsecs += ts->tv_nsec;
    return nsecs;
}

static inline long long
spectousec(struct timespec *ts)
{
    long long usecs = spectonsec(ts);

    return usecs < 0 ? usecs : usecs / NSECS_PER_USEC;
}

static inline int
gettimespecofday(struct timespec *ts)
{
    struct timeval tv = {0, 0};
    int rc;

    rc = gettimeofday(&tv, NULL);
    if (rc >= 0) {
        ts->tv_sec = tv.tv_sec;
        ts->tv_nsec = tv.tv_usec / 1000LL;
    }
    return rc;
}

/* minuend minus subtrahend equals difference */
static inline void
tssub(struct timespec *minuend, struct timespec *subtrahend,
      struct timespec *difference)
{
    long long m, s, d;

    m = spectonsec(minuend);
    s = spectonsec(subtrahend);

    if (s < 0) {
        d = 0;
    } else if (m < 0) {
        d = -1;
    } else {
        m -= s;
        d = m < 0 ? 0 : m;
    }

    nsectospec(d, difference);
    return;
}

static inline void
tsadd(struct timespec *augend, struct timespec *addend, struct timespec *sum)
{
    long long aug, add;

    aug = spectonsec(augend);
    add = spectonsec(addend);

//    printf("aug: %Ld add: %Ld\n", aug, add);

    if (aug < 0 || add < 0)
        nsectospec(-1, sum);
    else if (LLONG_MAX - MAX(add,aug) < MAX(add,aug))
        nsectospec(LLONG_MAX, sum);
    else
        nsectospec(aug+add, sum);
    return;
}

#define tsGT(x,y) (tscmp((x), (y)) < 0)
#define tsGE(x,y) (tscmp((x), (y)) <= 0)
#define tsET(x,y) (tscmp((x), (y)) == 0)
#define tsNE(x,y) (tscmp((x), (y)) != 0)
#define tsLE(x,y) (tscmp((x), (y)) >= 0)
#define tsLT(x,y) (tscmp((x), (y)) > 0)

static inline int
tscmp(struct timespec *a, struct timespec *b)
{
    long long m, s;
    long long rc;

    m = spectonsec(a);
    s = spectonsec(b);

    if (s < 0) {
        rc = 1;
        if (m < 0)
            rc = 0;
    } else if (m < 0) {
        rc = -1;
    } else {
        rc = MIN(MAX(s-m, -1), 1);
    }

    return rc;
}

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

#define save_errno(tmp, unsafe_code) ({     \
        (tmp) = errno ;                     \
        unsafe_code ;                       \
        errno = (tmp) ;                     \
    })

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_PRIV_UTIL_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
