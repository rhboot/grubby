#ifndef BDEVID_PROBE_H
#define BDEVID_PROBE_H 1

struct bdevid_probe_result;
extern const char *
bdevid_pr_getattr(struct bdevid_probe_result *r, const char *key);

typedef int (*bdevid_probe_cb)(struct bdevid_probe_result *, void *priv);
extern int
bdevid_probe(struct bdevid *, char *file, bdevid_probe_cb, void *priv);

#endif /* BDEVID_PROBE_H */

/*
 * vim:ts=8:sw=4:sts=4:et
 */
