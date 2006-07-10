#ifndef BDEVID_MODULE_H
#define BDEVID_MODULE_H 1

#include <sys/types.h>

/* the API for a module */

struct bdevid_module;
struct bdevid_module_ops {
    u_int32_t magic;
    char *name;
    int (*init)(struct bdevid_module *);
};
#define BDEVID_MAGIC 0x07d007f0
#define BDEVID_MODULE(__ops) \
    struct bdevid_module_ops *bdevid_module_ops = & (__ops) 

struct bdevid_probe_ops {
    char *name;
    int (*probe)(int fd, char **id);
};

extern int bdevid_register_probe(struct bdevid_module *,
    struct bdevid_probe_ops *ops);

#endif /* BDEVID_MODULE_H */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
