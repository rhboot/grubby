#ifndef BDEVID_H
#define BDEVID_H 1

#include <sys/types.h>

struct bdevid;

struct bdevid_module {
	u_int32_t magic;
	char *name;
	int (*init)(struct bdevid *, struct bdevid_module *);
};
#define BDEVID_MAGIC 0x07d007f0
#define BDEVID_MODULE(__module) \
	struct bdevid_module *bdevid_module = & (__module) 

struct bdevid_probe_ops {
	int (*probe)(dev_t dev, char **id);
};

extern int bdevid_register_probe(struct bdevid_module *,
	struct bdevid_probe_ops *ops);

#endif /* BDEVID_H */

/*
 * vim:ts=8:sts=8:sw=8:noet
 */
