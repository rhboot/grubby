#ifndef BDEVID_PRIV_H
#define BDEVID_PRIV_H 1

#include <bdevid.h>

struct bdevid;
struct bdevid_module;

struct bdevid {
	struct modloader *modloader;
	struct bdevid_module **modules;
	int nmodules;
};

struct bdevid_module {
	struct bdevid *bdevid;
	struct modloader_module *modloader_module;
	struct bdevid_module_ops *ops;
	struct bdevid_probe_ops **probes;
	int nprobes;
	void *priv;
};

#endif /* BDEVID_PRIV_H */
