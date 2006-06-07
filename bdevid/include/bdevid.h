#ifndef BDEVID_H
#define BDEVID_H 1

#include <sys/types.h>

struct bdevid_probe_ops {
	int (*probe)(int fd, char **id);
};

struct bdevid_module_context;

extern int bdevid_register_probe(struct bdevid_module_context *c,
	struct bdevid_probe_ops *ops);

struct bdevid_module_context;

struct bdevid_module_info {
	u_int32_t magic;
	char *name;
	int (*init)(struct bdevid_module_context *c);
};

#define BDEVID_INFO_MAGIC 0x07d007f0

#define BDEVID_MODULE(__info) \
	struct bdevid_module_info *bdevid_module_info = & (__info) 


#endif /* BDEVID_H */

/*
 * vim:ts=8:sts=8:sw=8:noet
 */
