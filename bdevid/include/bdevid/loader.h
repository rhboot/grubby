#ifndef BDEVID_LOADER_H
#define BDEVID_LOADER_H 1

struct bdevid;
struct bdevid_module;

/* the API a loader uses: */
extern struct bdevid *bdevid_new(char *env);
extern void bdevid_destroy(struct bdevid *);

extern int bdevid_path_set(struct bdevid *, char *path);
extern char *bdevid_path_get(struct bdevid *);

typedef int (*bdevid_device_probe_visitor)(const char *module,
    const char *probe, const char *id);
extern int bdevid_probe_device(struct bdevid *b, char *file,
    bdevid_device_probe_visitor visitor, void *user_data);

extern int bdevid_module_load(struct bdevid *, char *file);
extern int bdevid_module_unload(struct bdevid_module *);

/* XXX need module iter ? */

extern int bdevid_module_load_all(struct bdevid *);
extern int bdevid_module_unload_all(struct bdevid *);

#endif /* BDEVID_LOADER_H */

/*
 * vim:ts=8:sts=8:sw=8:noet
 */
