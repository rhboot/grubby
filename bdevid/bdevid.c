#include <stdio.h>
#include <modloader.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "bdevid.h"

static struct modloader_context *modcons;
static struct bdevid_module_info *minfo;

static int bdevid_module_cons(struct modloader_module_info *m, void **priv)
{
	void *dlh = modloader_module_dlhandle_get(m);
	struct bdevid_module_info **minfop;
	struct bdevid_module_context *c;
	
	c = calloc(1, sizeof (*c));
	modloader_module_priv_set(m, c);

	minfop = dlsym(dlh, "bdevid_module_info");
	if (!minfop || !*minfop)
		return -1;
	minfo = *minfop;
	if (!minfo->magic == BDEVID_INFO_MAGIC)
		return -1;

	/* XXX: create bdevid_module_context here */

	printf("bdevid: loading module '%s'\n", minfo->name);
	if (minfo->init) {
		int ret;

		if ((ret = minfo->init(c)) == -1)
			return -1;
	}
	return 0;
}

static int bdevid_module_dest(struct modloader_module_info *m, void **priv)
{
	return 0;
}

static int bdevid_module_ident(struct modloader_module_info *m, char **name)
{
	void *dlh = modloader_module_dlhandle_get(m);
	struct bdevid_module_info **minfop, *minfo;

	minfop = dlsym(dlh, "bdevid_module_info");
	if (!minfop || !*minfop)
		return -1;
	minfo = *minfop;
	if (!minfo->magic == BDEVID_INFO_MAGIC)
		return -1;
	printf("bdevid: found module '%s'\n", minfo->name);
	*name = minfo->name;
	return 0;
}

extern void bdevid_cons(void) __attribute__((constructor));
void bdevid_cons(void) {
	module_cons_dest *cd;
	module_ident *ident;

	if (!modcons)
		modcons = modloader_context_new();
	if (!modcons)
		return;
	
	cd = modloader_module_cons_get(modcons);
	*cd = bdevid_module_cons;
	cd = modloader_module_dest_get(modcons);
	*cd = bdevid_module_dest;

	ident = modloader_module_ident_get(modcons);
	*ident = bdevid_module_ident;

	modloader_path_set(modcons, "/lib/bdevid:/usr/lib/bdevid");
	modloader_path_env_set(modcons, "BDEVID_PATH");

	modloader_init(modcons);

	modloader_module_load_all(modcons);
}

extern void bdevid_dest(void) __attribute__((destructor));
void bdevid_dest(void) {
	modloader_context_destroy(modcons);
	modcons = NULL;
}

int bdevid_register_probe(struct bdevid_module_context *c,
	struct bdevid_probe_ops *ops)
{
	printf("module <%p> registered probe <%p>\n", c, ops);
	return 0;
}
