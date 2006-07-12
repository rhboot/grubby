#ifndef BDEVID_BDEV_H
#define BDEVID_BDEV_H 1

struct bdevid_bdev;
extern int bdevid_bdev_get_fd(struct bdevid_bdev *);
extern char *bdevid_bdev_get_name(struct bdevid_bdev *);
extern char *bdevid_bdev_get_sysfs_dir(struct bdevid_bdev *);

#endif /* BDEVID_BDEV_H */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
