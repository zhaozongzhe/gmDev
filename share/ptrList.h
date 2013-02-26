#ifndef _PTR_LIST_H
#define _PTR_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <malloc.h>
#include <memory.h>

struct ptrList
{
    void *data;
    struct ptrList *next;
    struct ptrList *prev;
};

typedef int (*ListCompareFunc)(const void *, const void *);
typedef void (*ListForeachFunc)(void *);

__inline static struct ptrList *node_alloc(void)
{
    struct ptrList *list = (struct ptrList *)malloc(sizeof(struct ptrList));
    memset(list, 0, sizeof(struct ptrList));
    return list;
}

__inline static void node_free(struct ptrList *node)
{
    if (node) free(node);
}

__inline int ptrList_size(const struct ptrList *list)
{
    int size = 0;

    while (list)
    {
        ++ size;
        list = list->next;
    }
    return size;
}

__inline void ptrList_sort(struct ptrList *list, ListCompareFunc comp_func)
{
    struct ptrList *li;
    int i, size;
    void **data;

    size = (int)ptrList_size(list);
    data = (void **)calloc(size, sizeof(void *));
    if (!data) return;
    for (i=0, li=list; i<size && li; i++, li=li->next)
        data[i] = li->data;
    qsort((void *)data, size, sizeof(void *), comp_func);
    for (i=0, li=list; i<size && li; i++, li=li->next)
        li->data = data[i];
    free(data);
}

__inline void ptrList_foreach(struct ptrList *list, ListForeachFunc foreach_func)
{
    for (; list; list = list->next)
        foreach_func(list->data);
}

__inline void ptrList_free(struct ptrList **list, ListForeachFunc data_free_func)
{
    while (*list)
    {
        struct ptrList *node = *list;
        *list = (*list)->next;
        if (data_free_func)
            data_free_func(node->data);
        node_free(node);
    }
}

__inline int ptrList_append(struct ptrList **list, void *data)
{
    struct ptrList *node = node_alloc();
    int size = 1;

    node->data = data;
    if (!*list)
        *list = node;
    else
    {
        struct ptrList *li = *list;
        while (li->next)
        {
            ++ size;
            li = li->next;
        }

        li->next = node;
        node->prev = li;
    }

    return size;
}

__inline void ptrList_insert(struct ptrList **list, void *data)
{
    struct ptrList *node = node_alloc();

    node->data = data;
    node->next = *list;
    if (*list)
    {
        if ((*list)->prev)
        {
            ((*list)->prev)->next = node;
            node->prev = (*list)->prev;
        }
        (*list)->prev = node;
    }
    *list = node;
}

static struct ptrList* ptrList_find_data(struct ptrList *list, const void *data)
{
    for (; list; list = list->next)
        if (list->data == data)
            return list;

    return NULL;
}

static void *ptrList_remove_node(struct ptrList **list, struct ptrList *node)
{
    void *data = node ? node->data : NULL;
    struct ptrList *prev = node ? node->prev : NULL;
    struct ptrList *next = node ? node->next : NULL;

    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (*list == node) *list = next;
    node_free(node);
    return data;
}

__inline void* ptrList_pop_front(struct ptrList **list)
{
    void *ret = NULL;

    if (*list)
    {
        ret = (*list)->data;
        ptrList_remove_node(list, *list);
    }
    return ret;
}

__inline void* ptrList_remove_data(struct ptrList **list, const void *data)
{
    return ptrList_remove_node(list, ptrList_find_data(*list, data));
}

__inline struct ptrList* ptrList_find(struct ptrList *list, const void *b, ListCompareFunc compare_func)
{
    for (; list; list = list->next)
        if (!compare_func(list->data, b))
            return list;

    return NULL;
}

__inline void* ptrList_remove(struct ptrList **list, const void *b, ListCompareFunc compare_func)
{
    return ptrList_remove_node(list, ptrList_find(*list, b, compare_func));
}

#ifdef __cplusplus
}
#endif

#endif
