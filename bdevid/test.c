/*
 * Copyright 2006 Red Hat, Inc.
 * Copyright 2003 IBM Corp.
 *
 * Authors:
 *	Patrick Mansfield <patmans@us.ibm.com>
 *	Peter Jones <pjones@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 */ 

#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <bdevid.h>

static int test_probe(dev_t dev, char **id)
{
	char *foo;

	foo = strdup("kerplow");
	if (!foo)
		return -1;
	*id = foo;
	printf("yowieee!");
	return 0;
}

struct bdevid_probe_ops test_probe_ops = {
	.probe = test_probe,
};

static int test_init(struct bdevid_module *bm)
{
	printf("in test_init\n");
	if (bdevid_register_probe(bm, &test_probe_ops) == -1)
		return -1;
	return 0;
}

struct bdevid_module_ops test_module_ops = {
	.magic = BDEVID_MAGIC,
	.name = "test",
	.init = test_init,
};

BDEVID_MODULE(test_module_ops);

/*
 * vim:ts=8:sts=8:sw=8:noet
 */
