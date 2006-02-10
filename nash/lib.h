/*
 * lib.h -- a small library for various parts of nash
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

#ifndef NASH_LIB_H
#define NASH_LIB_H 1

#define _GNU_SOURCE 1

#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>

extern int coeOpen(const char *path, int flags, ...);
extern FILE *coeFopen(const char *path, const char *mode);
extern DIR *coeOpendir(const char *name);

extern int readFD (int fd, char **buf);

extern int testing;
extern int quiet;
extern int reallyquiet;

typedef enum {
    NOTICE,
    WARNING,
    ERROR,
} nash_log_level;

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

extern int nashLoggerV(const nash_log_level level, const char *format,
        va_list ap)
    __attribute__((format(printf, 2, 0)));
extern int nashLogger(const nash_log_level level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));


extern int smartmknod(char * device, mode_t mode, dev_t dev);

extern int getDevNumFromProc(char * file, char * device);

extern int stringsort(const void *v0, const void *v1);

#endif /* NASH_LIB_H */
