#ifndef BDEVID_MODULE_H
#define BDEVID_MODULE_H 1

#include <sys/types.h>

/* the API for a module */

struct bdevid_module;
struct bdevid_probe_ops;

typedef int (*bdevid_register_func)(struct bdevid_module *,
    struct bdevid_probe_ops *);

struct bdevid_module_ops {
    u_int32_t magic;
    char *name;
    int (*init)(struct bdevid_module *, bdevid_register_func);
};
#define BDEVID_MAGIC 0x07d007f0
#define BDEVID_MODULE(__ops) \
    struct bdevid_module_ops *bdevid_module_ops = & (__ops) 

struct bdevid_probe_ops {
    char *name;
    int (*get_vendor)(struct bdevid_bdev *bdev, char **vendor);
    int (*get_model)(struct bdevid_bdev *bdev, char **model);
    int (*get_unique_id)(struct bdevid_bdev *bdev, char **id);
};

#endif /* BDEVID_MODULE_H */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
