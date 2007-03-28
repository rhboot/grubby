/*
 * list.c -- list operators
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2007 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>

#include "list.h"

struct nash_list {
    struct nash_list_operations *ops;
    struct nash_list_data *data;
};

void *
nash_list_get_data(struct nash_list *list)
{
    return list->data;
}

void
nash_list_set_data(struct nash_list *list, void *data)
{
    list->data = (struct nash_list_data *)data;
}

struct nash_list_operations *
nash_list_get_ops(struct nash_list *list)
{
    return list->ops;
}

void
nash_list_set_ops(struct nash_list *list, struct nash_list_operations *ops)
{
    list->ops = ops;
}

struct nash_list *
nash_list_new(struct nash_list_operations *ops)
{
    struct nash_list *list = NULL;

    if (!(list = calloc(1, sizeof (struct nash_list))))
        return NULL;

    list->ops = ops;
    if (list->ops->init(list) < 0) {
        free(list);
        return NULL;
    }
    return list;
}

struct nash_ptr_list_data {
    int nitems;
    void **items;
};

static int
nash_ptr_list_init(struct nash_list *list)
{
    struct nash_ptr_list_data *data = NULL;

    if (!(data = calloc(1, sizeof (*data))))
        return -1;
    nash_list_set_data(list, data);
    return 0;
}

static int
nash_ptr_list_len(struct nash_list *list)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    return data->nitems;
}

static inline int _nash_ptr_list_remove(struct nash_list_operations *ops,
    struct nash_ptr_list_data *data, int pos)
{
    void *entry;
    void **items;
    size_t size = sizeof (void *) * (data->nitems - pos - 1);

    if (pos < 0 || pos >= data->nitems) {
        errno = ENOENT;
        return -1;
    }
    entry = data->items[pos];

    if (data->nitems == 1) {
        free(data->items);
        data->items = NULL;
        data->nitems = 0;
        return 0;
    }

    data->nitems--;
    memmove(data->items+pos, data->items+pos+1, size);
    if (!(items = realloc(data->items, sizeof (void *) * data->nitems))) {
        int errnum = errno;
        memmove(data->items[pos+1], data->items[pos], size);
        data->items[pos] = entry;
        data->nitems++;
        errno = errnum;
        return -1;
    }
    data->items = items;
    return 0;
}

static inline int _nash_ptr_list_delete(struct nash_list_operations *ops,
    struct nash_ptr_list_data *data, int pos)
{
    void *entry = data->items[pos];

    if (pos < 0 || pos >= data->nitems) {
        errno = ENOENT;
        return -1;
    }
    entry = data->items[pos];
    if (_nash_ptr_list_remove(ops, data, pos) >= 0) {
        ops->free_item(entry);
        return 0;
    }
    return -1;
}

static int nash_ptr_list_remove(struct nash_list *list, int pos)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    struct nash_list_operations *ops = nash_list_get_ops(list);

    if (pos < 0 || pos >= data->nitems) {
        errno = ENOENT;
        return -1;
    }
    return _nash_ptr_list_remove(ops, data, pos);
}

static int nash_ptr_list_delete(struct nash_list *list, int pos)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    struct nash_list_operations *ops = nash_list_get_ops(list);

    if (pos < 0 || pos >= data->nitems) {
        errno = ENOENT;
        return -1;
    }
    return _nash_ptr_list_delete(ops, data, pos);
}

static int nash_ptr_list_destroy(struct nash_list *list)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    struct nash_list_operations *ops = nash_list_get_ops(list);

    while (data->nitems)
        _nash_ptr_list_delete(ops, data, 0);
    free(data);
    free(list);
    return 0;
}

static int _nash_ptr_list_add(struct nash_list *list, void *item, int ref)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    void **items, *new;
    int i;

    if (ref) {
        new = item;
    } else if (!(new = list->ops->dup_item(item))) {
        return -1;
    }

    if (data->nitems == 0) {
        if (!(items = calloc(2, sizeof (void *))))
            return -1;
        items[0] = new;
        data->items = items;
        data->nitems = 1;
        return 0;
    }

    if (!(items = realloc(data->items, sizeof (void *) * (data->nitems+2)))) {
        int errnum = errno;
        list->ops->free_item(new);
        errno = errnum;
        return -1;
    }
    i = data->nitems++;
    items[i] = new;
    items[data->nitems] = NULL;
    data->items = items;

    nash_list_sort(list);
    return i;
}

static int nash_ptr_list_add(struct nash_list *list, void *item)
{
    return _nash_ptr_list_add(list, item, 0);
}

static int nash_ptr_list_add_ref(struct nash_list *list, void *item)
{
    return _nash_ptr_list_add(list, item, 1);
}

static void *
nash_ptr_list_get(struct nash_list *list, int pos)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);

    if (pos < 0 || pos >= data->nitems) {
        errno = ENOENT;
        return NULL;
    }
    return data->items[pos];
}

static void
nash_generic_list_sort(struct nash_list *list)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    void **items;
    int i;

    if (!data || !(items = data->items))
        return;

    /* LAAAAZY */
    qsort(items, data->nitems, sizeof (void *), list->ops->cmp_item);
    /* it's too bad qsort doesn't dedupe... */
    for(i = 0; i < data->nitems-1; i++) {
	if (!list->ops->cmp_item(&items[i], &items[i+1])) {
            list->ops->free_item(items[i]);
            memmove(items+i, items+i+1, (data->nitems-i-1) * sizeof (void *));
            data->nitems--;
            i--;
        }
    }
    data->items = items;
}

static int
nash_ptr_list_search(struct nash_list *list, const void *target)
{
    struct nash_ptr_list_data *data = nash_list_get_data(list);
    size_t l, u, idx;
    void *item;
    int comparison;

    l = 0;
    u = data->nitems;

    while (l < u) {
        idx = (l + u) / 2;

        item = nash_list_get(list, idx);
        comparison = list->ops->cmp_item(&target, &item);

        if (comparison < 0)
            u = idx;
        else if (comparison > 0)
            l = idx + 1;
        else
            return idx;
    }

    return -1;
}

static void *nash_ptr_list_dup(const void *item)
{
    return (void *)item;
}

static void nash_ptr_list_free(const void *item)
{
    return;
}

static int nash_ptr_list_cmp(const void *a, const void *b)
{
    const void *aa = *(void **)a, *bb = *(void **)b;

    if (!aa || !bb)
        return (aa ? 1 : 0) - (bb ? 1 : 0);
    return bb - aa;
}

static int nash_str_list_cmp(const void *a, const void *b)
{
    char * const *as = a, * const *bs = b;
    if (!as || !bs)
        return (as ? 1 : 0) - (bs ? 1 : 0);
    return strcoll(*as, *bs);
}

struct nash_list_operations _nash_list_type_ops[] = {
    [NASH_UNSORTED_PTR_LIST] = {
        .init = nash_ptr_list_init,
        .destroy = nash_ptr_list_destroy,
        .len = nash_ptr_list_len,
        .sort = NULL,
        .add_item = nash_ptr_list_add,
        .add_item_ref = nash_ptr_list_add_ref,
        .del_item = nash_ptr_list_delete,
        .remove_item = nash_ptr_list_remove,
        .get_item = nash_ptr_list_get,
        .cmp_item = nash_ptr_list_cmp,
        .dup_item = nash_ptr_list_dup,
        .free_item = nash_ptr_list_free,
        .search = nash_ptr_list_search,
    },
    [NASH_PTR_LIST] = {
        .init = nash_ptr_list_init,
        .destroy = nash_ptr_list_destroy,
        .len = nash_ptr_list_len,
        .sort = nash_generic_list_sort,
        .add_item = nash_ptr_list_add,
        .add_item_ref = nash_ptr_list_add_ref,
        .del_item = nash_ptr_list_delete,
        .remove_item = nash_ptr_list_remove,
        .get_item = nash_ptr_list_get,
        .cmp_item = nash_ptr_list_cmp,
        .dup_item = nash_ptr_list_dup,
        .free_item = nash_ptr_list_free,
        .search = nash_ptr_list_search,
    },
    [NASH_STR_LIST] = {
        .init = nash_ptr_list_init,
        .destroy = nash_ptr_list_destroy,
        .len = nash_ptr_list_len,
        .sort = nash_generic_list_sort,
        .add_item = nash_ptr_list_add,
        .add_item_ref = nash_ptr_list_add_ref,
        .del_item = nash_ptr_list_delete,
        .remove_item = nash_ptr_list_remove,
        .get_item = nash_ptr_list_get,
        .cmp_item = nash_str_list_cmp,
        .dup_item = (nash_list_dup_item_func)strdup,
        .free_item = (nash_list_free_item_func)free,
        .search = nash_ptr_list_search,
    },
    [NASH_LIST_TYPE_END] = { NULL, }
};

static struct nash_list *nash_list_type_ops;

struct nash_list_operations *nash_list_get_type_ops(int type)
{
    struct nash_list *ops_table;
    struct nash_list_operations *ops;
    int i, errnum;

    ops_table = nash_list_type_ops;
    if (!ops_table) {
        ops_table = nash_list_new(&_nash_list_type_ops[NASH_UNSORTED_PTR_LIST]);
        if (!ops_table)
            return NULL;

        for (i = 0; i < NASH_LIST_TYPE_END; i++) {
            if (nash_list_add_ref(ops_table, &_nash_list_type_ops[i]) < 0)
                goto err;
        }
        nash_list_type_ops = ops_table;
    }

    i = 0;
    while (type >= 0 && (ops = nash_list_get(ops_table, i)))
        if (i++ == type)
            return ops;
    errno = ENOENT;
    return NULL;
err:
    errnum = errno;
    nash_list_destroy(ops_table);
    ops_table = NULL;
    errno = errnum;
    return NULL;
}

int nash_list_type_register(struct nash_list_operations *new_ops)
{
    if (nash_list_get_type_ops(-1) == NULL && errno != ENOENT)
        return -1;

    return nash_list_add(nash_list_type_ops, new_ops);
}

struct nash_list *nash_list_new_by_type(int type)
{
    struct nash_list_operations *ops = NULL;

    if (nash_list_get_type_ops(-1) == NULL && errno != ENOENT)
        return NULL;

    if ((ops = nash_list_get(nash_list_type_ops, type)) == NULL)
        return NULL;

    return nash_list_new(ops);
}

struct nash_list *nash_str_list_from_string(char *str, char *sep)
{
    struct nash_list *list = NULL;
    size_t pos = -1;

    if (!(list = nash_str_list_new()))
        return NULL;

    do {
        char *start, end;

        start = str+(++pos);
        pos += strcspn(start, sep);
        end = str[pos];
        str[pos] = '\0';

        if (nash_list_add(list, start) < 0) {
            str[pos] = end;
            goto err;
        }
        str[pos] = end;
    } while (str[pos]);

    return list;
err:
    nash_list_destroy(list);
    return NULL;
}

struct nash_list_iter {
    struct nash_list *list;
    int i;
};

void nash_list_iter_end(struct nash_list_iter *iter)
{
    if (iter) {
        memset(iter, '\0', sizeof (*iter));
        free(iter);
    }
}

struct nash_list_iter *nash_list_iter_next(struct nash_list *list,
    struct nash_list_iter *iter)
{
    void *item = NULL;
    if (iter == NULL && list == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (iter == NULL) {
        if (!(iter = calloc(1, sizeof (*iter))))
            return NULL;
        iter->list = list;
        iter->i = -1;
    }
    iter->i++;
    if (!(item = nash_list_get(iter->list, iter->i))) {
        errno = 0;
        nash_list_iter_end(iter);
        return NULL;
    }
    return iter;
}

int nash_list_iter_pos(struct nash_list_iter *iter)
{
    if (iter)
        return iter->i;
    return -1;
}

void *nash_list_iter_data(struct nash_list_iter *iter)
{
    if (!iter) {
        errno = EINVAL;
        return NULL;
    }
    return nash_list_get(iter->list, iter->i);
}

int nash_list_len(struct nash_list *list)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->len(list);
}

int nash_list_add(struct nash_list *list, void *item)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->add_item(list, item);
}

int nash_list_add_ref(struct nash_list *list, void *item)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->add_item_ref(list, item);
}

int nash_list_remove(struct nash_list *list, int pos)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->remove_item(list, pos);
}

int nash_list_delete(struct nash_list *list, int pos)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->del_item(list, pos);
}

void *nash_list_get(struct nash_list *list, int pos)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->get_item(list, pos);
}

int nash_list_destroy(struct nash_list *list)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->destroy(list);
}

void nash_list_sort(struct nash_list *list)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    if (ops->sort)
        ops->sort(list);
}

int nash_list_search(struct nash_list *list, void *item)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    return ops->search(list, item);
}

int nash_list_in(struct nash_list *list, void *item)
{
    struct nash_list_operations *ops = nash_list_get_ops(list);
    int n = ops->search(list, item);

    if (n >= 0)
        return 1;
    return 0;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
