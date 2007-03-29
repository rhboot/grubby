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

struct nashContext_s;

extern struct nashContext_s *_nash_context;

#define DEV_TYPE_NAME_EQUALS   0x1
#define DEV_TYPE_NAME_BEGINS   0x2

static struct dev_types {
    int type;
    char name[20];
    int major;
    int minor;
} dev_types[1024] = {
    /* pre-seeded with things that don't show up in /proc/devices :/ */
    { S_IFBLK, "ramdisk0", 0, -1 },
    { S_IFBLK, "ramdisk", 1, -1 },
    { S_IFBLK, "floppy", 2, -1 },
    { S_IFBLK, "ide0", 3, -1 },
    { S_IFBLK, "ide1", 22, -1 },
    { S_IFBLK, "ide2", 33, -1 },
    { S_IFBLK, "ide3", 34, -1 },
    { S_IFBLK, "ide4", 56, -1 },
    { S_IFBLK, "ide5", 57, -1 },
    { S_IFBLK, "ide6", 88, -1 },
    { S_IFBLK, "ide7", 89, -1 },
    { S_IFBLK, "ide8", 90, -1 },
    { S_IFBLK, "ide9", 91, -1 },
    { S_IFBLK, "ataraid", 114, -1 },
    { 0, "", 0, 0 }
};

static struct dev_type_names {
    char name[20];
    int flags;
    int type;
} dev_type_names[] = {
    /* these names needs to match the list in devtree.c */
    { "ataraid", DEV_TYPE_NAME_BEGINS, S_IFBLK },
    { "cciss", DEV_TYPE_NAME_BEGINS, S_IFBLK },
    { "dac960", DEV_TYPE_NAME_EQUALS, S_IFBLK },
    { "device-mapper", DEV_TYPE_NAME_EQUALS, S_IFCHR },
    { "device-mapper", DEV_TYPE_NAME_BEGINS, S_IFBLK },
    { "floppy", DEV_TYPE_NAME_EQUALS, S_IFBLK },
    { "i2o_block", DEV_TYPE_NAME_EQUALS, S_IFBLK },
    { "ida", DEV_TYPE_NAME_BEGINS, S_IFBLK },
    { "ide", DEV_TYPE_NAME_BEGINS, S_IFBLK },
    { "iseries/vd", DEV_TYPE_NAME_EQUALS, S_IFBLK },
    { "md", DEV_TYPE_NAME_EQUALS, S_IFBLK },
#if 0 || defined(NASH_SUPPORT_ABSURD_DEVICE_TOPOLOGY)
    { "mdp", DEV_TYPE_NAME_EQUALS, S_IFBLK },
#endif
    { "sd", DEV_TYPE_NAME_EQUALS, S_IFBLK },
    { "sx8", DEV_TYPE_NAME_BEGINS, S_IFBLK },
    { "", 0, 0 }
};

static int _dev_name_cmp(int pos, char *name, char **new)
{
    const struct dev_type_names *d;
    int l, m, n;
    int rc;
    
    l = strlen(name);
    m = strlen(dev_types[pos].name);
    /* exit early if the largest common subset doesn't match */
    if ((rc = strncmp(name, dev_types[pos].name, MIN(l,m))))
        return rc;
    /* steps:
     * 1) find the dev_type_names entry that corresponds with dev_types[pos]
     * 2) if it exists, see if that matches the name
     *    else, see if dev_types[pos].name matches exactly
     * 3) set new if appropriate
     * 4) return comparison
     */
    for (d = &dev_type_names[0]; d->name[0] != '\0'; d++) {
        if (d->type != dev_types[pos].type)
            continue;

        n = strlen(d->name);
        if (strncmp(dev_types[pos].name, d->name,
                    ((d->flags & DEV_TYPE_NAME_BEGINS) ? m : MAX(m,n))))
            continue;

        /* we have a match */
        rc = strncmp(d->name, name, 
                    ((d->flags & DEV_TYPE_NAME_BEGINS) ? n : MAX(l,n)));
        if (!rc)
            *new = (char *)&d->name[0];
        return rc;
    }
    rc = strcmp(dev_types[pos].name, name);
    if (!rc)
        *new = (char *)&d->name[0];
    return rc;
}

static void _getDevNumsFromProc(int majnum, int *pos)
{
    enum {
        MISC,
        CHAR,
        BLOCK
    } state = MISC;
    static const char *states[] = {
        [CHAR] = "Character devices:",
        [BLOCK] = "Block devices:",
    };
    FILE *f = NULL;
    char buf[80] = {'\0'};

    if (!(f = fopen(majnum == -1 ? "/proc/devices":"/proc/misc", "r")))
        return;

    while (*pos < 1023 && fgets(buf, sizeof (buf), f)) {
        int num, off;
        char *end;

        if (!(end = strchr(buf, '\n')))
            break;
        *end = '\0';
        if (!strncmp(buf, states[CHAR], strlen(states[CHAR]))) {
            state = CHAR;
            continue;
        } else if (!strncmp(buf, states[BLOCK], strlen(states[BLOCK]))) {
            state = BLOCK;
            continue;
        }
        if (sscanf(buf, "%d %n", &num, &off)) {
            switch (state) {
                case MISC:
                case CHAR:
                    dev_types[*pos].type = S_IFCHR;
                    break;
                case BLOCK:
                    dev_types[*pos].type = S_IFBLK;
                    break;
            }
            if (majnum >= 0) {
                dev_types[*pos].major = majnum;
                dev_types[*pos].minor = num;
            } else {
                dev_types[*pos].major = num;
                dev_types[*pos].minor = -1;
            }
            if (!strncmp(buf+off, "misc", 4)) {
                _getDevNumsFromProc(num, pos);
            } else {
                strncpy(dev_types[*pos].name, buf+off, 19);
                dev_types[*pos].name[19] = '\0';
                (*pos)++;
            }
        }
    }
    dev_types[*pos].type = 0;
    fclose(f);
}

static int first_allocated_entry = -1;
static void _setupGetDevNumsFromProc(void)
{
    if (first_allocated_entry < 0) {
        int i;
        for (i = 0; dev_types[i].name[0] != '\0'; i++)
            ;
        first_allocated_entry = i;
        _getDevNumsFromProc(-1, &i);
    }
}

static inline int getDevsFromProc(int pos, int type, char **name, dev_t *devno)
{
    _setupGetDevNumsFromProc();

    if (pos < -1 || pos > 1022)
        return -1;
    if ((!name || !*name) && !devno)
        return -1;

    while (dev_types[++pos].type != 0) {
        struct dev_types *d = &dev_types[pos];
        if (d->type == -1)
            break;
        if (type != -1 && type != d->type)
            continue;
        if (*name) {
            char *new = NULL;
            if (!_dev_name_cmp(pos, *name, &new)) {
                if (new && *name != new)
                    *name = new;
                *devno = makedev(d->major, d->minor);
                return pos;
            }
        } else {
            int dmaj = major(*devno);
            int dmin = minor(*devno);
            if ((dmaj == -1 || d->major == -1 || dmaj == d->major) &&
                    (dmin == -1 || d->minor == -1 || dmin == d->minor)) {
                *name = d->name;
                return pos;
            }
        }
    }
    return -1;
}

static inline int
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

static inline int
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

static inline int
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

extern int getDevsFromProc(int pos, int type, char **name, dev_t *devno);

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

    usectospec(usecs, &rem);
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

#define xfree(x) ({if (x) { free(x); x = NULL; }})
#define movptr(x, y) ({(x) = (y); (y) = NULL; })
#define xmovptr(x, y) ({if (!(y) && (x)) movptr((x),(y));})

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_PRIV_UTIL_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
