/*
 * dm.h - backend library for partition table scanning on dm devices
 * 
 * Copyright 2005 Peter M. Jones
 * Copyright 2005 Red Hat, Inc.
 * 
 * vim:ts=8:sw=4:sts=4:et
 *
 */

#ifndef NASH_DM_H
#define NASH_DM_H 1

typedef enum {
    NOTICE,
    WARNING,
    ERROR,
} nash_log_level;

extern int nashLogger(const nash_log_level level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

extern int
nashDmCreate(char *name, long long start, long long length,
        char *type, char *params);

extern int
nashDmRemove(char *name);

extern int
nashDmCreatePartitions(char *path);

#if 0 /* notyet */
extern int
nashDmRemovePartitions(char *name);
#endif

#endif /* NASH_DM_H */
