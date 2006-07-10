#ifndef BDEVID_PRIV_H
#define BDEVID_PRIV_H 1

#include <glib.h>
#include <bdevid.h>

struct bdevid;
struct bdevid_module;
struct bdevid_probe;

struct bdevid {
	char *module_pathz;
	size_t module_pathz_len;

	GHashTable *modules;
};

struct bdevid_module {
	struct bdevid *bdevid;

	char *name;
	void *dlh;

	struct bdevid_module_ops *ops;

	GPtrArray *probes;
};

struct bdevid_probe {
	struct bdevid_module *module;

	char *name;

	struct bdevid_probe_ops *ops;
};

#endif /* BDEVID_PRIV_H */
