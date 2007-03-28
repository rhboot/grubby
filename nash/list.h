/*
 * list.h -- list operators
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2007 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef NASH_PRIV_LIST_H
#define NASH_PRIV_LIST_H 1

#ifdef _GNU_SOURCE
#define _GNU_SOURCE_DEFINED
#else
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>

struct nash_list;

typedef void *(*nash_list_dup_item_func)(const void *);
typedef void (*nash_list_free_item_func)(const void *);
typedef int (*nash_list_cmp_item_func)(const void *, const void *);

struct nash_list_operations {
    int (*init)(struct nash_list *);
    int (*destroy)(struct nash_list *);
    int (*len)(struct nash_list *);
    void (*sort)(struct nash_list *);
    int (*add_item)(struct nash_list *, void *item);
    int (*add_item_ref)(struct nash_list *, void *item);
    int (*del_item)(struct nash_list *, int pos);
    int (*remove_item)(struct nash_list *, int pos);
    void *(*get_item)(struct nash_list *, int pos);
    int (*cmp_item)(const void *, const void *);
    void *(*dup_item)(const void *item);
    void (*free_item)(const void *item);
    int (*search)(struct nash_list *, const void *item);
};

extern int nash_list_type_register(struct nash_list_operations *);

enum {
    NASH_UNSORTED_PTR_LIST,
    NASH_PTR_LIST,
    NASH_STR_LIST,
    NASH_LIST_TYPE_END,
};

extern struct nash_list_operations *nash_list_get_type_ops(int type);
extern struct nash_list *nash_list_new_by_type(int type);
#define nash_str_list_new() nash_list_new_by_type(NASH_STR_LIST)
#define nash_ptr_list_new() nash_list_new_by_type(NASH_PTR_LIST)

extern struct nash_list *nash_list_new(struct nash_list_operations *ops);

extern struct nash_list *nash_str_list_from_string(char *str, char *sep);

extern struct nash_list_operations *nash_list_get_ops(struct nash_list *);
extern void nash_list_set_ops(struct nash_list *,
    struct nash_list_operations *);
extern void *nash_list_get_data(struct nash_list *);
extern void nash_list_set_data(struct nash_list *, void *);

extern int nash_list_len(struct nash_list *list);
extern int nash_list_add(struct nash_list *list, void *item);
extern int nash_list_add_ref(struct nash_list *list, void *item);
extern int nash_list_remove(struct nash_list *list, int pos);
extern int nash_list_delete(struct nash_list *list, int pos);
extern void *nash_list_get(struct nash_list *list, int pos);
extern int nash_list_destroy(struct nash_list *list);
extern void nash_list_sort(struct nash_list *list);
extern int nash_list_search(struct nash_list *list, void *item);
extern int nash_list_in(struct nash_list *list, void *item);

struct nash_list_iter;
extern void nash_list_iter_end(struct nash_list_iter *);
extern struct nash_list_iter *nash_list_iter_next(struct nash_list *,
    struct nash_list_iter *);
extern int nash_list_iter_pos(struct nash_list_iter *);
extern void *nash_list_iter_data(struct nash_list_iter *);

extern void *nash_list_get_data(struct nash_list *list);
extern void nash_list_set_data(struct nash_list *list, void *);

extern struct nash_list_operations *nash_list_get_ops(struct nash_list *);
extern void nash_list_set_ops(struct nash_list *, struct nash_list_operations *);

#ifndef _GNU_SOURCE_DEFINED
#undef _GNU_SOURCE
#else
#undef _GNU_SOURCE_DEFINED
#endif

#endif /* NASH_PRIV_LIST_H */
/*
 * vim:ts=8:sw=4:sts=4:et
 */
