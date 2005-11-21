#ifndef NASH_H
#define NASH_H 1

#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>

extern int
coeOpen(const char *path, int flags, ...)
    __attribute__ ((__nonnull__(1)));

extern FILE *
coeFopen(const char *path, const char *mode)
    __attribute__ ((__nonnull__(1, 2)));

extern DIR *
coeOpendir(const char *name)
    __attribute__ ((__nonnull__(1)));

#endif /* NASH_H */
