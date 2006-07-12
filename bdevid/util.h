/*
 * util.h -- a small collection of utility functions
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef BDEVID_PRIV_UTIL_H
#define BDEVID_PRIV_UTIL_H 1

#include <stdlib.h>

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


#endif /* BDEVID_PRIV_UTIL_H */
