#ifndef _BENCODE_H
#define _BENCODE_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <io.h>
#include <math.h>
#include <locale.h>
#include <errno.h>

#include "buffer.h"
#include "helper.h"

enum
{
    BENC_TYPE_INT  = 1,
    BENC_TYPE_STR  = 2,
    BENC_TYPE_LIST = 4,
    BENC_TYPE_DICT = 8,
    BENC_TYPE_BOOL = 16,
    BENC_TYPE_REAL = 32
};

struct benc
{
    union
    {
        uint8_t b;              /* bool type */

        double d;               /* double type */

        int64_t i;              /* int type */

        struct                  /* string type */
        {
            size_t len;         /* the string length */
            union {
                char buf[16];   /* local struct buffer for short strings */
                char *ptr;      /* alloc'ed pointer for long strings */
            } str;
        } s;

        struct                  /* list & dict types */
        {
            struct benc *vals;  /* nodes */
            size_t alloc;       /* nodes allocated */
            size_t count;       /* nodes used */
        } l;
    } val;

    char type;
};

int benc_parse(void *buf, void *bufend, struct benc *setme_benc, uint8_t **setme_end);
int benc_load(void *buf, size_t buflen, struct benc *setme_benc, char **setme_end);

void benc_free(struct benc *);

void benc_initRaw(struct benc *, void *raw, size_t raw_len);
void benc_initStr(struct benc *, void *str, int str_len);
void benc_initInt(struct benc *, int64_t num);
int benc_initDict(struct benc *, size_t reserveCount);
int benc_initList(struct benc *, size_t reserveCount);
void benc_initBool(struct benc *, int value);
void benc_initReal(struct benc *, double value);

int benc_toFile(struct benc *, TCHAR *filename);
char* benc_toStr(struct benc *, int *len);
void benc_toBuf(struct benc *, struct buffer *);

int benc_listReserve(struct benc *, size_t reserveCount);
struct benc *benc_listAdd(struct benc *);
struct benc *benc_listAddBool(struct benc *, BOOL val);
struct benc *benc_listAddInt(struct benc *, int64_t val);
struct benc *benc_listAddReal(struct benc *, double val);
struct benc *benc_listAddStr(struct benc *, char *val);
struct benc *benc_listAddRaw(struct benc *, uint8_t *val, size_t len);
struct benc *benc_listAddList(struct benc *, size_t reserveCount);
struct benc *benc_listAddDict(struct benc *, size_t reserveCount);
size_t benc_listSize(struct benc *list);
struct benc *benc_listChild(struct benc * list, size_t n);

int benc_dictReserve(struct benc *, size_t reserveCount);
int benc_dictRemove(struct benc *, char *key);
struct benc *benc_dictAdd(struct benc *, char *key);
struct benc *benc_dictAddReal(struct benc *, char *key, double);
struct benc *benc_dictAddInt(struct benc *, char *key, int64_t);
struct benc *benc_dictAddBool(struct benc *, char *key, BOOL);
struct benc *benc_dictAddStr(struct benc *, char *key, char *);
struct benc *benc_dictAddList(struct benc *, char *key, size_t reserve);
struct benc *benc_dictAddDict(struct benc *, char *key, size_t reserve);
struct benc *benc_dictAddRaw(struct benc *, char *key, void *raw, size_t rawlen);
BOOL benc_dictChild(struct benc *, size_t i, char **key, struct benc **val);
struct benc *benc_dictFind(struct benc *, char *key);
BOOL benc_dictFindList(struct benc *, char *key, struct benc **setme);
BOOL benc_dictFindDict(struct benc *, char *key, struct benc **setme);
BOOL benc_dictFindInt(struct benc *, char *key, int64_t *setme);
BOOL benc_dictFindReal(struct benc *, char *key, double *setme);
BOOL benc_dictFindBool(struct benc *, char *key, BOOL *setme);
BOOL benc_dictFindStr(struct benc *, char *key, char **setme);
BOOL benc_dictFindRaw(struct benc *, char *key, uint8_t **setme_raw, size_t *setme_len);

/** Get an int64_t from a variant object
    return TRUE if successful, or FALSE if the variant could not be represented as an int64_t  */
BOOL benc_getInt(struct benc *val, int64_t *setme);

/** Get an string from a variant object
    return TRUE if successful, or FALSE if the variant could not be represented as a string  */
BOOL benc_getStr(struct benc *val, char **setme);
BOOL benc_getStrLen(struct benc *val, size_t *str_len);

/** Get a boolean from a variant object
    return TRUE if successful, or FALSE if the variant could not be represented as a boolean  */
BOOL benc_getBool(struct benc *val, BOOL *setme);

/** Get a floating-point number from a variant object
    return TRUE if successful, or FALSE if the variant could not be represented as a floating-point number  */
BOOL benc_getReal(struct benc *val, double *setme);

static inline BOOL benc_isType(struct benc * b, int type) { return (b != NULL) && (b->type == type); }

static inline BOOL benc_isInt(struct benc * b) { return benc_isType(b, BENC_TYPE_INT); }
static inline BOOL benc_isDict(struct benc * b) { return benc_isType(b, BENC_TYPE_DICT); }
static inline BOOL benc_isList(struct benc * b) { return benc_isType(b, BENC_TYPE_LIST); }
static inline BOOL benc_isString(struct benc * b) { return benc_isType(b, BENC_TYPE_STR); }
static inline BOOL benc_isBool(struct benc * b) { return benc_isType(b, BENC_TYPE_BOOL); }
static inline BOOL benc_isReal(struct benc * b) { return benc_isType(b, BENC_TYPE_REAL); }

/** Private function that's exposed here only for unit tests */
int benc_parseInt(uint8_t *buf, uint8_t *bufend, uint8_t **setme_end, int64_t *setme_val);

/** Private function that's exposed here only for unit tests */
int benc_parseStr(uint8_t *buf, uint8_t *bufend,
    uint8_t **setme_end, uint8_t **setme_str, size_t *setme_strlen);

/* this is only quasi-supported.  don't rely on it too heavily outside of libT */
void benc_mergeDicts(struct benc *target, struct benc *source);

#ifdef __cplusplus
}
#endif

#endif
