/*
 * command.c - command interface for libbdevid
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <bdevid.h>
#include <nash.h>

static
void usage(FILE *f, char *name)
{
    fprintf(f, "usage: %s <devpec>\n", name);
}

static char *
escape(const char *in)
{
    char *ret = NULL, *out = NULL;
    int i = 0;
    char *quote = (char *)in;

    size_t len = strlen(in);

    while ((quote = strchr(quote, '"')))
        i++;

    if (!i)
        return strdup(in);

    len += i;
    if (!(ret = out = calloc(len, sizeof (char))))
        return NULL;

    strcpy(out, in);

    while (*in) {
        if (*in == '"') {
            *out++ = '\\';
            *out++ = '"';
        } else {
            *out++ = *in;
        }
        in++;
    }

    return ret;
}

static int
probe_visitor(struct bdevid_probe_result *result, void *priv)
{
    char *attrs[] = {"MODULE", "PROBE", "VENDOR", "MODEL", "UNIQUE_ID", NULL};
    int i;
    char *dev;

    if (!(dev = escape(priv)))
        return -1;

    printf("DEV=\"%s\"", dev); 
    free(dev);
    for (i = 0; attrs[i]; i++) {
        char *value = NULL;

        if (!(value = (char *)bdevid_pr_getattr(result, attrs[i])))
            continue;

        if (!(value = escape(value)))
            continue;

        printf(" %s=\"%s\"", attrs[i], value);
        free(value);
    }
    printf("\n");
    return 0;
}

int main(int argc, char *argv[])
{
    struct bdevid *bdevid;
    nashContext *nc;
    int i;

    if (argc < 2) {
        usage(stderr, argv[0]);
        return 1;
    }

    if (!(bdevid = bdevid_new("BDEVID_PATH"))) {
        perror("bdevid");
        return 2;
    }

    if (bdevid_module_load_all(bdevid) < 0) {
        perror("bdevid");
        return 3;
    }

    if (!(nc = nashNewContext())) {
        perror("bdevid");
        return 4;
    }

    for (i = 1; i < argc; i++) {
        char *device = nashGetPathBySpec(nc, argv[i]);

        if (!device)
            continue;
        bdevid_probe(bdevid, device, probe_visitor, device);
    }

    nashFreeContext(nc);
    bdevid_destroy(bdevid);
    return 0;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
