#define _GNU_SOURCE 1

#include <stdio.h>
#include <bdevid.h>

static int scsi_probe(int fd, char **id)
{
	return 0;
}

struct bdevid_probe_ops scsi_probe_ops = {
	.probe = scsi_probe,
};

static int scsi_init(struct bdevid_module_context *c)
{
	printf("in scsi_init\n");
	if (bdevid_register_probe(c, &scsi_probe_ops) == -1)
		return -1;
	return 0;
}

struct bdevid_module_info scsi_module_info = {
	.magic = BDEVID_INFO_MAGIC,
	.name = "scsi",
	.init = scsi_init,
};

BDEVID_MODULE(scsi_module_info);

