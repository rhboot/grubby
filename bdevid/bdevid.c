#define _GNU_SOURCE 1

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <modloader.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "bdevid.h"

static int bdevid_module_cons(struct modloader_module *mm)
{
	void *dlh = modloader_module_dlhandle_get(mm);
	struct bdevid_module **bmodp, *bmod;
	struct bdevid *b;
	
	b = calloc(1, sizeof (*b));
	modloader_module_priv_set(mm, b);

	bmodp = dlsym(dlh, "bdevid_module");
	if (!bmodp || !*bmodp)
		return -1;
	bmod = *bmodp;
	if (!bmod->magic == BDEVID_MAGIC)
		return -1;

	printf("bdevid: loading module '%s'\n", bmod->name);
	if (bmod->init && bmod->init(b, bmod) == -1)
		return -1;
	return 0;
}

static void bdevid_module_dest(struct modloader_module *mm)
{
	assert(0);
}

static int bdevid_module_ident(struct modloader_module *mm, char **name)
{
	void *dlh = modloader_module_dlhandle_get(mm);
	struct bdevid_module **bmodp, *bmod;

	bmodp = dlsym(dlh, "bdevid_module");
	if (!bmodp || !*bmodp)
		return -1;
	bmod = *bmodp;
	if (!bmod->magic == BDEVID_MAGIC)
		return -1;
	printf("bdevid: found module '%s'\n", bmod->name);
	*name = bmod->name;
	return 0;
}

struct modloader_ops bdevid_ops = {
	.path_env = "BDEVID_PATH",
	.constructor = bdevid_module_cons,
	.destructor = bdevid_module_dest,
	.ident = bdevid_module_ident,
};

void bdevid_destroy(struct bdevid *b)
{
	if (b) {
		if (b->modloader)
			modloader_destroy(b->modloader);
		memset(b, '\0', sizeof (*b));
		free(b);
	}
}

struct bdevid *bdevid_new(void)
{
	struct bdevid *b = NULL;

	if (!(b = calloc(1, sizeof (*b))))
		goto err;

	if (!(b->modloader = modloader_new(&bdevid_ops)))
		goto err;

	modloader_path_set(b->modloader, "/lib/bdevid:/usr/lib/bdevid");
	modloader_module_load_all(b->modloader);

	return b;
err:
	bdevid_destroy(b);
	return NULL;
}

int bdevid_register_probe(struct bdevid_module *bm,
	struct bdevid_probe_ops *ops)
{
	printf("module <%p> registered probe <%p>\n", bm, ops);
	return 0;
}

int bdevid_probe_device(struct bdevid *b, char *path, char **id)
{
	assert(0);
	return 1;	
}

/*
 * vim:ts=8:sts=8:sw=8:noet
 */
