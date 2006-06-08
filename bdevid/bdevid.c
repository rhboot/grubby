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

static inline int bdevid_module_add(struct bdevid *b, struct bdevid_module *m)
{
	struct bdevid_module **modules = NULL;

	modules = realloc(b->modules, sizeof (*m) * (b->nmodules+2));
	if (!modules)
		return -1;
	
	modules[b->nmodules++] = m;
	modules[b->nmodules] = NULL;
	b->modules = modules;

	return 0;
}

static inline int bdevid_module_remove(struct bdevid_module *m)
{
	if (m) {
		struct bdevid *b = NULL;
		struct bdevid_module **modules = NULL;
		int i;

		b = m->bdevid;

		for (i=0; b->modules[i] && b->modules[i] != m; i++)
			;
		if (!b->modules[i])
			return 0;

		if (!(modules = realloc(b->modules,sizeof (*m) * b->nmodules)))
			return -1;

		if (i != b->nmodules) 
			modules[i] = modules[b->nmodules];
		modules[b->nmodules--] = NULL;
		b->modules = modules;
	}
	return 0;
}

static void bdevid_module_dest(struct modloader_module *mm)
{
	struct bdevid_module *bm = modloader_module_priv_get(mm);

	if (bm) {
		if (bm->probes)
			free(bm->probes);

		memset(bm, '\0', sizeof (*bm));
		free(bm);
		modloader_module_priv_set(mm, NULL);
	}
}

static int bdevid_module_cons(struct modloader_module *mm)
{
	struct modloader *m = modloader_module_modloader_get(mm);
	struct bdevid *b = modloader_priv_get(m);

	void *dlh = modloader_module_dlhandle_get(mm);
	struct bdevid_module_ops **ops;
	struct bdevid_module *bm = NULL;
	int added = 0;

	if (!(bm = calloc(1, sizeof (*bm))))
		goto err;
	modloader_module_priv_set(mm, bm);
	bm->modloader_module = mm;
	
	ops = dlsym(dlh, "bdevid_module_ops");
	if (!ops || !*ops)
		goto err;

	bm->ops = *ops;
	if (!(bm->ops->magic == BDEVID_MAGIC))
		goto err;

	if (!(bm->probes = calloc(1, sizeof (struct bdevid_probe_ops *))))
		goto err;

	b = modloader_priv_get(m);
	if (bdevid_module_add(b, bm) == -1)
		goto err;
	added = 1;
	
	printf("bdevid: loading module '%s'\n", bm->ops->name);
	if (bm->ops->init && bm->ops->init(bm) == -1)
		goto err;

	return 0;
err:
	if (bm) {
		if (added)
			bdevid_module_remove(bm);
		bdevid_module_dest(mm);
	}
	return -1;
}

static int bdevid_module_ident(struct modloader_module *mm, char **name)
{
	void *dlh = modloader_module_dlhandle_get(mm);
	struct bdevid_module_ops **ops;

	ops = dlsym(dlh, "bdevid_module_ops");
	if (!ops || !*ops)
		return -1;

	if (!((*ops)->magic != BDEVID_MAGIC))
		return -1;

	printf("bdevid: found module '%s'\n", (*ops)->name);
	*name = (*ops)->name;
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
		while (b->modules && b->modules[0]) {
			struct bdevid_module *bm = b->modules[0];
			struct modloader_module *mm = bm->modloader_module;

			bdevid_module_remove(bm);
			bdevid_module_dest(mm);
		}
		if (b->modules)
			free(b->modules);
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
	
	if (!(b->modules = calloc(1, sizeof (struct bdevid_module *))))
		goto err;

	modloader_path_set(b->modloader, "/lib/bdevid:/usr/lib/bdevid");
	modloader_priv_set(b->modloader, b);
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
