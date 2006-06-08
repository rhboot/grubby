#ifndef BDEVID_H
#define BDEVID_H 1

#include <sys/types.h>

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
	int (*probe)(dev_t dev, char **id);
};

extern int bdevid_register_probe(struct bdevid_module *,
	struct bdevid_probe_ops *ops);

extern void *bdevid_module_priv_get(struct bdevid_module *);
extern int bdevid_module_priv_set(struct bdevid_module *, void *);


/* the API a loader uses: */
extern struct bdevid *bdevid_new(void);
extern void bdevid_destroy(struct bdevid *);
extern int bdevid_probe_device(struct bdevid *, char *path, char **id);

#endif /* BDEVID_H */

/*
 * vim:ts=8:sts=8:sw=8:noet
 */
