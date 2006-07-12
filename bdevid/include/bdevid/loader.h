#ifndef BDEVID_LOADER_H
#define BDEVID_LOADER_H 1

struct bdevid;
struct bdevid_module;

/* the API a loader uses: */
extern struct bdevid *bdevid_new(char *env);
extern void bdevid_destroy(struct bdevid *);

extern int bdevid_path_set(struct bdevid *, char *path);
extern char *bdevid_path_get(struct bdevid *);

extern int bdevid_module_load_file_maybe(struct bdevid *b, char *file,
                                         char *name);
extern int bdevid_module_load_file(struct bdevid *b, char *file);
extern int bdevid_module_load(struct bdevid *, char *name);
extern int bdevid_module_unload(struct bdevid *, char *name);

/* XXX need module iter ? */

extern int bdevid_module_load_all(struct bdevid *);
extern int bdevid_module_unload_all(struct bdevid *);

#endif /* BDEVID_LOADER_H */

/*
 * vim:ts=8:sts=4:sw=4:et
 */
