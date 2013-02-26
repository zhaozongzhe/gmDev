#ifndef _PTR_ARRAY_H_
#define _PTR_ARRAY_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ptrArrayCompareFunc)(const void *, const void *);
typedef void (*ptrArrayForEachFunc)(void *);

struct ptrArray
{
    void **items;
    int n_items;
    int n_alloc;
    ptrArrayCompareFunc funcCompare;
};

__inline int ptrArray_size(struct ptrArray *a)
{
    return (a->n_items);
}

__inline int ptrArray_isEmpty(struct ptrArray * a)
{
    return (a->n_items == 0);
}

__inline void *ptrArray_nth(struct ptrArray *a, int n)
{
    if (n >= a->n_items) n = (a->n_items-1);
    return (a->items[n]);
}

__inline void *ptrArray_back(struct ptrArray * a)
{
    return a->n_items > 0 ? ptrArray_nth(a, a->n_items - 1 ) : NULL;
}

__inline void* ptrArray_pop(struct ptrArray *a)
{
    if (a->n_items) return a->items[--a->n_items];
    return NULL;
}

__inline void ptrArray_clear(struct ptrArray *a)
{
    a->n_items = 0;
}

__inline void ptrArray_erase(struct ptrArray *a, int begin, int end)
{
    if (end > a->n_items) end = a->n_items;

    memmove(&a->items[begin], &a->items[end], sizeof(void *)*(a->n_items-end));
    a->n_items -= (end - begin);
}

static int ptrArray_defFuncCompare(const void *p1, const void *p2)
{
    if (p1 < p2)
        return -1;
    else if (p1 == p2)
        return 0;
    else
        return 1;
}

static int ptrArray_lowerBound(const struct ptrArray *a, const void *ptr, int *exact_match)
{
    int len = a->n_items;
    int first = 0;
    ptrArrayCompareFunc funcCompare =
        a->funcCompare ? a->funcCompare : ptrArray_defFuncCompare;

    while (len > 0)
    {
        int half = len / 2;
        int middle = first + half;
        const int c = funcCompare(a->items[middle], ptr);
        if (c == 0)
        {
            if (exact_match) *exact_match = 1;
            return middle;
        }
        if (c < 0)
        {
            first = middle + 1;
            len = len - half - 1;
        }
        else len = half;
    }
    if (exact_match) *exact_match = 0;
    return first;
}

static int ptrArray_insert(struct ptrArray *a, void *insertMe, int pos)
{
    if (a->n_items >= a->n_alloc)
    {
        a->n_alloc = __max(64, (a->n_items * 3) / 2);
        if (a->items != NULL)
        {
            void **pTmp = (void **)realloc(a->items, sizeof(void *)*a->n_alloc);
            if (!pTmp) return (int)0xFFFFFFFF;
            a->items = pTmp;
        }
        else a->items = (void **)malloc(sizeof(void *)*a->n_alloc);
        if (!a->items) return (int)0xFFFFFFFF;
    }

    if (pos >= a->n_items) pos = a->n_items;
    else memmove(&a->items[pos+1], &a->items[pos], sizeof(void *)*(a->n_items-pos));

    a->items[pos] = insertMe;
    a->n_items++;
    return pos;
}

__inline int ptrArray_insertSorted(struct ptrArray *a, void *ptr)
{
    int match;
    int pos = ptrArray_lowerBound(a, ptr, &match);

    if (match) return ((int)0xFFFFFFFF);
    else return ptrArray_insert(a, ptr, pos);
}

__inline int ptrArray_append(struct ptrArray *a, void *ptr)
{
    return ptrArray_insert(a, ptr, a->n_items);
}

__inline void *ptrArray_findSorted(struct ptrArray *a, const void *ptr)
{
    int match, pos;
    pos = ptrArray_lowerBound(a, ptr, &match);
    return (match ? a->items[pos] : NULL);
}

__inline void *ptrArray_removeSorted(struct ptrArray *a, const void *ptr)
{
    void *ret = NULL;
    int match, pos;

    pos = ptrArray_lowerBound(a, ptr, &match);
    if (match)
    {
        ret = a->items[pos];
        ptrArray_erase(a, pos, pos + 1);
    }
    return ret;
}

__inline void ptrArray_init(struct ptrArray *a, ptrArrayCompareFunc funcCompare)
{
    a->items = NULL;
    a->n_items = 0;
    a->n_alloc = 0;
    a->funcCompare = funcCompare;
}

__inline void ptrArray_free(struct ptrArray *a, ptrArrayForEachFunc funcFree)
{
    if (funcFree)
    {
        int i;
        for (i=0; i<a->n_items; i++)
            funcFree(a->items[i]);
    }

    if (a->items != NULL) free(a->items);
    a->items = NULL;
    a->n_items = 0;
    a->n_alloc = 0;
}

#ifdef __cplusplus
}
#endif

#endif
