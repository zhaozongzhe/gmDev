#include "bencode.h"

static BOOL isContainer(struct benc *val)
{
    return benc_isList(val) || benc_isDict(val);
}

static BOOL isSomething(struct benc *val)
{
    return isContainer(val) || benc_isInt(val)
                            || benc_isString(val)
                            || benc_isReal(val)
                            || benc_isBool(val);
}

static void benc_init(struct benc *val, int type)
{
    memset(val, 0, sizeof(*val));
    val->type = (char)type;
}

/**
 *The initial i and trailing e are beginning and ending delimiters.
 *You can have negative numbers such as i-3e. You cannot prefix the
 *number with a zero such as i04e. However, i0e is valid.
 *Example: i3e represents the integer "3"
 *NOTE: The maximum number of bit of this integer is unspecified,
 *but to handle it as a signed 64bit integer is mandatory to handle
 *"large files" aka .torrent for more that 4Gbyte
 */
int benc_parseInt(uint8_t *buf, uint8_t *bufend,
                  uint8_t **setme_end, int64_t *setme_val)
{
    char *endptr;
    void *begin;
    void *end;
    int64_t val;

    if (buf >= bufend)
        return EILSEQ;
    if (*buf != 'i')
        return EILSEQ;

    begin = buf + 1;
    end = memchr(begin, 'e', (bufend - buf) - 1);
    if (end == NULL)
        return EILSEQ;

    errno = 0;
    val = _strtoi64((char *)begin, &endptr, 10);
    if (errno || (endptr != end)) /*incomplete parse */
        return EILSEQ;
    if (val && *(char*)begin == '0') /*no leading zeroes! */
        return EILSEQ;

    *setme_end = (uint8_t*)end + 1;
    *setme_val = val;
    return 0;
}

/**
 *Byte strings are encoded as follows:
 *<string length encoded in base ten ASCII>:<string data>
 *Note that there is no constant beginning delimiter, and no ending delimiter.
 *Example: 4:spam represents the string "spam"
 */
int benc_parseStr(uint8_t *buf, uint8_t *bufend,
                  uint8_t **setme_end, uint8_t **setme_str,
                  size_t *setme_strlen)
{
    size_t len;
    void *end;
    char *endptr;

    if (buf >= bufend)
        return EILSEQ;

    if (!isdigit(*buf ))
        return EILSEQ;

    end = memchr(buf, ':', bufend - buf);
    if (end == NULL)
        return EILSEQ;

    errno = 0;
    len = strtoul((char*)buf, &endptr, 10);
    if (errno || endptr != end)
        return EILSEQ;

    if ((uint8_t*)end + 1 + len > bufend)
        return EILSEQ;

    *setme_end = (uint8_t*)end + 1 + len;
    *setme_str = (uint8_t*)end + 1;
    *setme_strlen = len;
    return 0;
}

/*set to 1 to help expose bugs with benc_listAdd and benc_dictAdd */
#define LIST_SIZE 4 /*number of items to increment list/dict struct buffer by */

static int makeroom(struct benc *val, size_t count)
{
    if (BENC_TYPE_LIST != val->type && BENC_TYPE_DICT != val->type)
        return 1;

    if (val->val.l.count + count > val->val.l.alloc)
    {
        /*We need a bigger boat */
        int len = val->val.l.alloc + count +
                        ((count % LIST_SIZE) ?
                        (LIST_SIZE - (count % LIST_SIZE)) : 0);
        void *tmp = realloc(val->val.l.vals, len *sizeof(struct benc));
        if (!tmp)
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = (struct benc *)tmp;
    }

    return 0;
}

static struct benc* getNode(struct benc *top, struct ptrArray *parentStack, int type)
{
    struct benc *parent;

    if (ptrArray_isEmpty(parentStack))
        return top;

    parent = (struct benc *)ptrArray_back(parentStack);

    /*dictionary keys must be strings */
    if ((parent->type == BENC_TYPE_DICT)
        && (type != BENC_TYPE_STR)
        && (!(parent->val.l.count % 2)))
        return NULL;

    makeroom(parent, 1);
    return parent->val.l.vals + parent->val.l.count++;
}

/**
 *This function's previous recursive implementation was
 *easier to read, but was vulnerable to a smash-stacking
 *attack via maliciously-crafted bencoded data. (#667)
 */
static int benc_parseImpl(void *buf_in, void *bufend_in,
        struct benc *top, struct ptrArray *parentStack, uint8_t **setme_end)
{
    int err;
    uint8_t *buf = (uint8_t *)buf_in;
    uint8_t *bufend = (uint8_t *)bufend_in;

    benc_init(top, 0);

    while (buf != bufend)
    {
        if (buf > bufend) /*no more text to parse... */
            return 1;

        if (*buf == 'i') /*int */
        {
            int64_t val;
            uint8_t *end;
            struct benc *node;

            if ((err = benc_parseInt(buf, bufend, &end, &val)))
                return err;

            node = getNode(top, parentStack, BENC_TYPE_INT);
            if (!node)
                return EILSEQ;

            benc_initInt(node, val);
            buf = end;

            if (ptrArray_isEmpty(parentStack))
                break;
        }
        else if (*buf == 'l') /*list */
        {
            struct benc *node = getNode(top, parentStack, BENC_TYPE_LIST);
            if (!node)
                return EILSEQ;
            benc_init(node, BENC_TYPE_LIST);
            ptrArray_append(parentStack, node);
            ++buf;
        }
        else if (*buf == 'd') /*dict */
        {
            struct benc *node = getNode(top, parentStack, BENC_TYPE_DICT);
            if (!node)
                return EILSEQ;
            benc_init(node, BENC_TYPE_DICT);
            ptrArray_append(parentStack, node);
            ++buf;
        }
        else if (*buf == 'e') /*end of list or dict */
        {
            struct benc *node;
            ++buf;
            if (ptrArray_isEmpty(parentStack))
                return EILSEQ;

            node = (struct benc *)ptrArray_back(parentStack);
            if (benc_isDict(node) && (node->val.l.count % 2))
            {
                /*odd # of children in dict */
                benc_free(&node->val.l.vals[--node->val.l.count]);
                return EILSEQ;
            }

            ptrArray_pop(parentStack);
            if (ptrArray_isEmpty(parentStack))
                break;
        }
        else if (isdigit(*buf)) /*string? */
        {
            uint8_t *end;
            uint8_t *str;
            size_t str_len;
            struct benc *node;

            if ((err = benc_parseStr(buf, bufend, &end, &str, &str_len)))
                return err;

            node = getNode(top, parentStack, BENC_TYPE_STR);
            if (!node)
                return EILSEQ;

            benc_initStr(node, str, str_len);
            buf = end;

            if (ptrArray_isEmpty(parentStack))
                break;
        }
        else /*invalid bencoded text... march past it */
        {
            ++buf;
        }
    }

    err = !isSomething(top) || !ptrArray_isEmpty(parentStack);

    if (!err && setme_end)
        *setme_end = buf;

    return err;
}

int benc_parse(void *buf, void *end, struct benc *top, uint8_t **setme_end)
{
    int err;
    struct ptrArray parentStack = { NULL, 0, 0, NULL };

    top->type = 0; /*set to `uninitialized' */
    err = benc_parseImpl(buf, end, top, &parentStack, setme_end);
    if (err)
        benc_free(top);

    ptrArray_free(&parentStack, NULL);
    return err;
}

int benc_load(void *buf_in, size_t buflen, struct benc *setme_benc, char **setme_end)
{
    uint8_t *buf = (uint8_t *)buf_in;
    uint8_t *end;
    int ret = benc_parse(buf, buf + buflen, setme_benc, &end);

    if (!ret && setme_end)
        *setme_end = (char*)end;
    return ret;
}

/***
****
***/

/*returns true if the struct benc's string was malloced.
 *this occurs when the string is too long for our string struct buffer */
static inline int stringIsAlloced(struct benc *val)
{
    return val->val.s.len >= sizeof(val->val.s.str.buf);
}

/*returns a pointer to the struct benc's string */
static inline char*getStr(struct benc*val)
{
    return stringIsAlloced(val) ? val->val.s.str.ptr : val->val.s.str.buf;
}

static int dictIndexOf(struct benc *val, char *key)
{
    if (benc_isDict(val))
    {
        size_t i;
        size_t len = strlen(key);

        for (i = 0; (i + 1) < val->val.l.count; i += 2)
        {
            struct benc *child = val->val.l.vals + i;
            if ((child->type == BENC_TYPE_STR)
                && (child->val.s.len == len)
                && !memcmp(getStr(child), key, len))
                return i;
        }
    }

    return -1;
}

struct benc *benc_dictFind(struct benc *val, char *key)
{
    int i = dictIndexOf(val, key);
    return i < 0 ? NULL : &val->val.l.vals[i + 1];
}

static BOOL benc_dictFindType(struct benc *dict, char *key, int type, struct benc **setme)
{
    return benc_isType(*setme = benc_dictFind(dict, key), type);
}

size_t benc_listSize(struct benc *list)
{
    return benc_isList(list) ? list->val.l.count : 0;
}

struct benc *benc_listChild(struct benc *val, size_t i)
{
    struct benc *ret = NULL;

    if (benc_isList(val) && (i < val->val.l.count))
        ret = val->val.l.vals + i;
    return ret;
}

static void benc_warning(char *err)
{
    fprintf(stderr, "warning: %s\n", err);
}

BOOL benc_getInt(struct benc *val, int64_t *setme)
{
    BOOL success = FALSE;

    if (!success && ((success = benc_isInt(val))))
        if (setme)
            *setme = val->val.i;

    if (!success && ((success = benc_isBool(val))))
    {
        benc_warning("reading bool as an int");
        if (setme)
            *setme = val->val.b ? 1 : 0;
    }

    return success;
}

BOOL benc_getStr(struct benc *val, char **setme)
{
    int success = benc_isString(val);
    if (success)
        *setme = getStr(val);
    return success;
}

BOOL benc_getStrLen(struct benc *val, size_t *str_len)
{
    int success = benc_isString(val);
    if (success)
        *str_len = val->val.s.len;
    return success;
}

BOOL benc_getBool(struct benc *val, BOOL *setme)
{
    char *str;
    BOOL success = FALSE;

    if ((success = benc_isBool(val)))
        *setme = val->val.b;

    if (!success && benc_isInt(val))
        if ((success = (val->val.i==0 || val->val.i==1)))
            *setme = val->val.i!=0;

    if (!success && benc_getStr(val, &str))
        if ((success = (!strcmp(str, "true") || !strcmp(str, "false"))))
            *setme = !strcmp(str,"true");

    return success;
}

BOOL benc_getReal(struct benc *val, double *setme)
{
    BOOL success = FALSE;

    if (!success && ((success = benc_isReal(val))))
        *setme = val->val.d;

    if (!success && ((success = benc_isInt(val))))
        *setme = (double)val->val.i;

    if (!success && benc_isString(val))
    {
        char *endptr;
        char locale[128];
        double d;

        /*the json spec requires a '.' decimal point regardless of locale */
        strcpy(locale, setlocale(LC_NUMERIC, NULL));
        setlocale(LC_NUMERIC, "POSIX");
        d  = strtod(getStr(val), &endptr);
        setlocale(LC_NUMERIC, locale);

        if ((success = (getStr(val) != endptr) && !*endptr))
            *setme = d;
    }


    return success;
}

BOOL benc_dictFindInt(struct benc *dict, char *key, int64_t *setme)
{
    return benc_getInt(benc_dictFind(dict, key), setme);
}

BOOL benc_dictFindBool(struct benc *dict, char *key, BOOL *setme)
{
    return benc_getBool(benc_dictFind(dict, key), setme);
}

BOOL benc_dictFindReal(struct benc *dict, char *key, double *setme)
{
    return benc_getReal(benc_dictFind(dict, key), setme);
}

BOOL benc_dictFindStr(struct benc *dict, char *key, char **setme)
{
    return benc_getStr(benc_dictFind(dict, key), setme);
}

BOOL benc_dictFindList(struct benc *dict, char *key, struct benc **setme)
{
    return benc_dictFindType(dict, key, BENC_TYPE_LIST, setme);
}

BOOL benc_dictFindDict(struct benc *dict, char *key, struct benc **setme)
{
    return benc_dictFindType(dict, key, BENC_TYPE_DICT, setme);
}

BOOL benc_dictFindRaw(struct benc *dict, char *key,
    uint8_t **setme_raw, size_t *setme_len)
{
    struct benc *child;
    BOOL found = benc_dictFindType(dict, key, BENC_TYPE_STR, &child);

    if (found) {
        *setme_raw = (uint8_t*) getStr(child);
        *setme_len = child->val.s.len;
    }
    return found;
}

/***
****
***/

void benc_initRaw(struct benc *val, void *src, size_t byteCount)
{
    char *setme;
    benc_init(val, BENC_TYPE_STR);

    /*There's no way in struct benc notation to distinguish between
     *zero-terminated strings and raw byte arrays.
     *Because of this, benc_mergeDicts() and benc_listCopy()
     *don't know whether or not a BENC_TYPE_STR node needs a '\0'.
     *Append one, een to the raw arrays, just to be safe. */

    if (byteCount < sizeof(val->val.s.str.buf))
        setme = val->val.s.str.buf;
    else
        setme = val->val.s.str.ptr = (char *)malloc(byteCount + 1);

    if (!setme) return;
    memcpy(setme, src, byteCount);
    setme[byteCount] = '\0';
    val->val.s.len = byteCount;
}

void benc_initStr(struct benc *val, void *str, int len)
{
    if (str == NULL)
        len = 0;
    else if (len < 0)
        len = strlen((char *)str);

    benc_initRaw(val, str, len);
}

void benc_initBool(struct benc *b, int value)
{
    benc_init(b, BENC_TYPE_BOOL);
    b->val.b = value != 0;
}

void benc_initReal(struct benc *b, double value)
{
    benc_init(b, BENC_TYPE_REAL);
    b->val.d = value;
}

void benc_initInt(struct benc *b, int64_t value)
{
    benc_init(b, BENC_TYPE_INT);
    b->val.i = value;
}

int benc_initList(struct benc *b, size_t reserveCount)
{
    benc_init(b, BENC_TYPE_LIST);
    return benc_listReserve(b, reserveCount);
}

int benc_listReserve(struct benc *b, size_t count)
{
    return makeroom(b, count);
}

int benc_initDict(struct benc *b, size_t reserveCount)
{
    benc_init(b, BENC_TYPE_DICT);
    return benc_dictReserve(b, reserveCount);
}

int benc_dictReserve(struct benc *b, size_t reserveCount)
{
    return makeroom(b, reserveCount * 2);
}

struct benc *benc_listAdd(struct benc *list)
{
    struct benc *item;

    if (list->val.l.count == list->val.l.alloc)
        benc_listReserve(list, LIST_SIZE);

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    benc_init(item, BENC_TYPE_INT);

    return item;
}

struct benc *benc_listAddInt(struct benc *list, int64_t val)
{
    struct benc *node = benc_listAdd(list);

    benc_initInt(node, val);
    return node;
}

struct benc *benc_listAddReal(struct benc *list, double val)
{
    struct benc *node = benc_listAdd(list);
    benc_initReal(node, val);
    return node;
}

struct benc *benc_listAddBool(struct benc *list, BOOL val)
{
    struct benc *node = benc_listAdd(list);
    benc_initBool(node, val);
    return node;
}

struct benc *benc_listAddStr(struct benc *list, char *val)
{
    struct benc *node = benc_listAdd(list);
    benc_initStr(node, val, -1);
    return node;
}

struct benc *benc_listAddRaw(struct benc *list, uint8_t *val, size_t len)
{
    struct benc *node = benc_listAdd(list);
    benc_initRaw(node, val, len);
    return node;
}

struct benc *benc_listAddList(struct benc *list, size_t reserveCount)
{
    struct benc *child = benc_listAdd(list);
    benc_initList(child, reserveCount);
    return child;
}

struct benc *benc_listAddDict(struct benc *list, size_t reserveCount)
{
    struct benc *child = benc_listAdd(list);
    benc_initDict(child, reserveCount);
    return child;
}

struct benc *benc_dictAdd(struct benc *dict, char *key)
{
    struct benc *keyval, *itemval;

    if (dict->val.l.count + 2 > dict->val.l.alloc)
        makeroom(dict, 2);

    keyval = dict->val.l.vals + dict->val.l.count++;
    benc_initStr(keyval, key, -1);

    itemval = dict->val.l.vals + dict->val.l.count++;
    benc_init(itemval, BENC_TYPE_INT);

    return itemval;
}

static struct benc *dictFindOrAdd(struct benc *dict, char *key, int type)
{
    struct benc *child;

    /*see if it already exists, and if so, try to reuse it */
    if ((child = benc_dictFind(dict, key)))
    {
        if (!benc_isType(child, type))
        {
            benc_dictRemove(dict, key);
            child = NULL;
        }
    }

    /*if it doesn't exist, create it */
    if (child == NULL)
        child = benc_dictAdd(dict, key);

    return child;
}

struct benc *benc_dictAddInt(struct benc *dict, char *key, int64_t val)
{
    struct benc *child = dictFindOrAdd(dict, key, BENC_TYPE_INT);
    benc_initInt(child, val);
    return child;
}

struct benc *benc_dictAddBool(struct benc *dict, char *key, BOOL val)
{
    struct benc *child = dictFindOrAdd(dict, key, BENC_TYPE_BOOL);
    benc_initBool(child, val);
    return child;
}

struct benc *benc_dictAddReal(struct benc *dict, char *key, double val)
{
    struct benc *child = dictFindOrAdd(dict, key, BENC_TYPE_REAL);
    benc_initReal(child, val);
    return child;
}

struct benc *benc_dictAddStr(struct benc *dict, char *key, char *val)
{
    struct benc *child;

    /*see if it already exists, and if so, try to reuse it */
    if ((child = benc_dictFind(dict, key)))
    {
        if (benc_isString(child))
        {
            if (stringIsAlloced(child))
                free(child->val.s.str.ptr);
        }
        else
        {
            benc_dictRemove(dict, key);
            child = NULL;
        }
    }

    /*if it doesn't exist, create it */
    if (child == NULL)
        child = benc_dictAdd(dict, key);

    /*set it */
    benc_initStr(child, val, -1);

    return child;
}

struct benc *benc_dictAddRaw(struct benc *dict, char *key, void *src, size_t len)
{
    struct benc *child;

    /*see if it already exists, and if so, try to reuse it */
    if ((child = benc_dictFind(dict, key)))
    {
        if (benc_isString(child))
        {
            if (stringIsAlloced(child))
                free(child->val.s.str.ptr);
        }
        else
        {
            benc_dictRemove(dict, key);
            child = NULL;
        }
    }

    /*if it doesn't exist, create it */
    if (child == NULL)
        child = benc_dictAdd(dict, key);

    /*set it */
    benc_initRaw(child, src, len);

    return child;
}

struct benc *benc_dictAddList(struct benc *dict, char *key, size_t reserveCount)
{
    struct benc *child = benc_dictAdd(dict, key);
    benc_initList(child, reserveCount);
    return child;
}

struct benc *benc_dictAddDict(struct benc *dict, char *key, size_t reserveCount)
{
    struct benc *child = benc_dictAdd(dict, key);
    benc_initDict(child, reserveCount);
    return child;
}

int benc_dictRemove(struct benc *  dict, char *key)
{
    int i = dictIndexOf(dict, key);

    if (i >= 0)
    {
        int n = dict->val.l.count;
        benc_free(&dict->val.l.vals[i]);
        benc_free(&dict->val.l.vals[i + 1]);
        if (i + 2 < n)
        {
            dict->val.l.vals[i]   = dict->val.l.vals[n - 2];
            dict->val.l.vals[i + 1] = dict->val.l.vals[n - 1];
        }
        dict->val.l.count -= 2;
    }
    return i >= 0; /*return true if found */
}

/***
**** BENC WALKING
***/

struct key_index
{
    char *key;
    int index;
};

static int compareKeyIndex(const void *va, const void *vb)
{
    struct key_index *a = (struct key_index *)va;
    struct key_index *b = (struct key_index *)vb;
    return strcmp(a->key, b->key);
}

struct save_node
{
    struct benc *val;
    int valIsVisited;
    int childCount;
    int childIndex;
    int *children;
};

#define NEW(struct_type, n_structs)           \
    ((struct_type *)malloc(((size_t)sizeof(struct_type)) * ((size_t)(n_structs))))
#define NEW0(struct_type, n_structs)           \
    ((struct_type *)calloc(1, ((size_t)sizeof(struct_type)) * ((size_t)(n_structs))))

static struct save_node *nodeNewDict(struct benc *val)
{
    int i, j, nKeys;
    struct save_node *node;
    struct key_index *indices;

    nKeys = val->val.l.count / 2;
    node = NEW0(struct save_node, 1);
    node->val = val;
    node->children = NEW0(int, nKeys * 2);

    /* ugh, a dictionary's children have to be sorted by key... */
    indices = NEW(struct key_index, nKeys);
    if (!indices) return NULL;
    for (i = j = 0; i < (nKeys * 2); i += 2, ++j)
    {
        indices[j].key = getStr(&val->val.l.vals[i]);
        indices[j].index = i;
    }
    qsort(indices, j, sizeof(struct key_index), compareKeyIndex);
    for (i = 0; i < j; ++i)
    {
        int index = indices[i].index;
        node->children[node->childCount++] = index;
        node->children[node->childCount++] = index + 1;
    }

    if (node->childCount != nKeys *2)
        printf("error\r\n");
    free(indices);
    return node;
}

static struct save_node *nodeNewList(struct benc *val)
{
    int i, n;
    struct save_node *node;

    n = val->val.l.count;
    node = NEW0(struct save_node, 1);
    node->val = val;
    node->childCount = n;
    node->children = NEW0(int, n);
    for (i = 0; i < n; ++i) /* a list's children don't need to be reordered */
        node->children[i] = i;

    return node;
}

static struct save_node *nodeNewLeaf(struct benc *val)
{
    struct save_node *node;

    node = NEW0(struct save_node, 1);
    node->val = val;
    return node;
}

static struct save_node *nodeNew(struct benc *val)
{
    struct save_node *node;

    if (benc_isList(val))
        node = nodeNewList(val);
    else if (benc_isDict(val))
        node = nodeNewDict(val);
    else
        node = nodeNewLeaf(val);

    return node;
}

typedef void (*BencWalkFunc)(struct benc *val, void *user_data);

struct WalkFuncs
{
    BencWalkFunc intFunc;
    BencWalkFunc boolFunc;
    BencWalkFunc realFunc;
    BencWalkFunc stringFunc;
    BencWalkFunc dictBeginFunc;
    BencWalkFunc listBeginFunc;
    BencWalkFunc containerEndFunc;
};

/**
 *This function's previous recursive implementation was
 *easier to read, but was vulnerable to a smash-stacking
 *attack via maliciously-crafted bencoded data. (#667)
 */
static void bencWalk(struct benc *top,
    struct WalkFuncs *walkFuncs,
    void *user_data)
{
    struct ptrArray stack = { NULL, 0, 0, NULL };

    ptrArray_append(&stack, nodeNew(top));

    while (!ptrArray_isEmpty(&stack))
    {
        struct save_node *node = (struct save_node *)ptrArray_back(&stack);
        struct benc *val;

        if (!node) break;
        if (!node->valIsVisited)
        {
            val = node->val;
            node->valIsVisited = TRUE;
        }
        else if (node->childIndex < node->childCount)
        {
            int index = node->children[node->childIndex++];
            val = node->val->val.l.vals +  index;
        }
        else /*done with this node */
        {
            if (isContainer(node->val))
                walkFuncs->containerEndFunc(node->val, user_data);
            ptrArray_pop(&stack);
            if (node->children) free(node->children);
            if (node) free(node);
            continue;
        }

        if (val) switch(val->type)
        {
        case BENC_TYPE_INT:
            walkFuncs->intFunc(val, user_data);
            break;
        case BENC_TYPE_BOOL:
            walkFuncs->boolFunc(val, user_data);
            break;
        case BENC_TYPE_REAL:
            walkFuncs->realFunc(val, user_data);
            break;
        case BENC_TYPE_STR:
            walkFuncs->stringFunc(val, user_data);
            break;
        case BENC_TYPE_LIST:
            if (val != node->val)
                ptrArray_append(&stack, nodeNew(val));
            else
                walkFuncs->listBeginFunc(val, user_data);
            break;
        case BENC_TYPE_DICT:
            if (val != node->val)
                ptrArray_append(&stack, nodeNew(val));
            else
                walkFuncs->dictBeginFunc(val, user_data);
            break;
        default:
            /*did caller give us an uninitialized val? */
            printf("error: invalid metadata\r\n");
            break;
        }
    }

    ptrArray_free(&stack, free);
}

/****
*****
****/

static void saveIntFunc(struct benc *val, void *bf)
{
    char szTmp[256];
    int len;
    len = _snprintf(szTmp, 256, "i" "%I64d" "e", val->val.i);
    buffer_append((struct buffer *)bf, szTmp, len);
}

static void saveBoolFunc(struct benc *val, void *bf)
{
    if (val->val.b)
        buffer_append((struct buffer *)bf, "i1e", 3);
    else
        buffer_append((struct buffer *)bf, "i0e", 3);
}

static void saveRealFunc(struct benc *val, void *bf)
{
    char buf[256], buf1[128];
    char locale[128];
    size_t len, len1;

    /*always use a '.' decimal point s.t. locale-hopping doesn't bite us */
    strcpy(locale, setlocale(LC_NUMERIC, NULL));
    setlocale(LC_NUMERIC, "POSIX");
    len = _snprintf(buf, sizeof(buf), "%f", val->val.d);
    setlocale(LC_NUMERIC, locale);

    len1 = _snprintf(buf1, sizeof(buf1), "%lu:", (unsigned long)len);
    buffer_append((struct buffer *)bf, buf1, len1);
    buffer_append((struct buffer *)bf, buf, len);
}

static void saveStringFunc(struct benc *val, void *bf)
{
    char buf[256];
    size_t len;

    len = _snprintf(buf, sizeof(buf), "%lu:", (unsigned long)val->val.s.len);
    buffer_append((struct buffer *)bf, buf, len);
    buffer_append((struct buffer *)bf, getStr(val), val->val.s.len);
}

static void saveDictBeginFunc(struct benc *val, void *bf)
{
    buffer_append((struct buffer *)bf, "d", 1);
}

static void saveListBeginFunc(struct benc *val, void *bf)
{
    buffer_append((struct buffer *)bf, "l", 1);
}

static void saveContainerEndFunc(struct benc *val, void *bf)
{
    buffer_append((struct buffer *)bf, "e", 1);
}

static struct WalkFuncs saveFuncs = { saveIntFunc,
                                      saveBoolFunc,
                                      saveRealFunc,
                                      saveStringFunc,
                                      saveDictBeginFunc,
                                      saveListBeginFunc,
                                      saveContainerEndFunc };

/***
****
***/

static void freeDummyFunc(struct benc *val, void *bf)
{
}

static void freeStringFunc(struct benc *val, void *freeme)
{
    if (stringIsAlloced(val))
        ptrArray_append((struct ptrArray *)freeme, val->val.s.str.ptr);
}

static void freeContainerBeginFunc(struct benc *val, void *freeme)
{
    ptrArray_append((struct ptrArray *)freeme, val->val.l.vals);
}

static struct WalkFuncs freeWalkFuncs = { freeDummyFunc,
                                          freeDummyFunc,
                                          freeDummyFunc,
                                          freeStringFunc,
                                          freeContainerBeginFunc,
                                          freeContainerBeginFunc,
                                          freeDummyFunc };

void benc_free(struct benc *val)
{
    if (isSomething(val))
    {
        struct ptrArray a = { NULL, 0, 0, NULL };
        bencWalk(val, &freeWalkFuncs, &a);
        ptrArray_free(&a, free);
    }
}

/***
****
***/

static void benc_listCopy(struct benc *target, struct benc *src)
{
    int i = 0;
    struct benc *val;

    while ((val = benc_listChild((struct benc*)src, i++)))
    {
       if (benc_isBool(val))
       {
           BOOL boolVal = 0;
           benc_getBool(val, &boolVal);
           benc_listAddBool(target, boolVal);
       }
       else if (benc_isReal(val))
       {
           double realVal = 0;
           benc_getReal(val, &realVal);
           benc_listAddReal(target, realVal);
       }
       else if (benc_isInt(val))
       {
           int64_t intVal = 0;
           benc_getInt(val, &intVal);
           benc_listAddInt(target, intVal);
       }
       else if (benc_isString(val))
           benc_listAddRaw(target, (uint8_t*)getStr(val), val->val.s.len);
       else if (benc_isDict(val))
           benc_mergeDicts(benc_listAddDict(target, 0), val);
       else if (benc_isList(val))
           benc_listCopy(benc_listAddList(target, 0), val);
       else
           printf("benc_listCopy skipping item\r\n");
   }
}

static size_t benc_dictSize(struct benc *dict)
{
    size_t count = 0;

    if (benc_isDict(dict))
        count = dict->val.l.count / 2;

    return count;
}

BOOL benc_dictChild(struct benc *dict, size_t n, char **key, struct benc **val)
{
    BOOL success = 0;

    if (benc_isDict(dict) && (n*2)+1 <= dict->val.l.count)
    {
        struct benc *k = dict->val.l.vals + (n*2);
        struct benc *v = dict->val.l.vals + (n*2) + 1;
        if ((success = benc_getStr(k, key) && isSomething(v)))
            *val = v;
    }

    return success;
}

void benc_mergeDicts(struct benc *target, struct benc *source)
{
    size_t i;
    size_t sourceCount = benc_dictSize(source);

    for (i=0; i<sourceCount; ++i)
    {
        char *key;
        struct benc *val;
        struct benc *t;

        if (benc_dictChild((struct benc*)source, i, &key, &val))
        {
            if (benc_isBool(val))
            {
                BOOL boolVal;
                benc_getBool(val, &boolVal);
                benc_dictAddBool(target, key, boolVal);
            }
            else if (benc_isReal(val))
            {
                double realVal = 0;
                benc_getReal(val, &realVal);
                benc_dictAddReal(target, key, realVal);
            }
            else if (benc_isInt(val))
            {
                int64_t intVal = 0;
                benc_getInt(val, &intVal);
                benc_dictAddInt(target, key, intVal);
            }
            else if (benc_isString(val))
                benc_dictAddRaw(target, key, getStr(val), val->val.s.len);
            else if (benc_isDict(val) && benc_dictFindDict(target, key, &t))
                benc_mergeDicts(t, val);
            else if (benc_isList(val))
            {
                if (benc_dictFind(target, key) == NULL)
                    benc_listCopy(benc_dictAddList(target, key, benc_listSize(val)), val);
            }
            else
                printf("benc_mergeDicts skipping \"%s\"", key);
        }
    }
}

/***
****
***/

void benc_toBuf(struct benc *top, struct buffer *buf)
{
    buffer_clear(buf);
    buffer_alloc(buf, 4096); /*alloc a little memory to start off with */

    bencWalk(top, &saveFuncs, buf);
}

char *benc_toStr(struct benc *top, int *len)
{
    struct buffer buf;

    buffer_init(&buf);
    benc_toBuf(top, &buf);
    if (len) *len = buf.len;
    return (char *)buf.buff;
}

int benc_toFile(struct benc *top, TCHAR *filename)
{
    int len, result = 0;
    char *str = benc_toStr(top, &len);
    if (!SetFileContent(filename, str, len)) result = -1;
    free(str);
    return result;
}

