/*
 * block.h -- find/create block devices and their fs nodes
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

#ifndef NASH_PRIV_BLOCK_H
#define NASH_PRIV_BLOCK_H 1

extern void block_init(void);
extern void block_finish(void);
extern char *block_find_fs_by_label(const char *label);
extern char *block_find_fs_by_uuid(const char *uuid);
extern char *block_find_fs_by_name(const char *name);
extern char *block_find_device_by_devno(dev_t devno);

extern void block_show_labels(void);

extern void sysfs_blkdev_probe(const char *dirname);

extern char *agetpathbyspec(const char * spec);
#define getpathbyspec(_spec) ({         \
        char *_ret0, *_ret1 = NULL;     \
        _ret0 = agetpathbyspec(_spec);  \
        if (_ret0) {                    \
            _ret1 = strdupa(_ret0);     \
            free(_ret0);                \
        }                               \
        _ret1;                          \
    })

extern int mkpathbyspec(const char *spec, const char *path);

extern int block_disable_partitions(const char *devname);

#endif /* NASH_PRIV_BLOCK_H */
