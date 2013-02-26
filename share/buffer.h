#ifndef _BUFFER_H
#define _BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <malloc.h>

struct buffer
{
    int len;
    int alloc;
    unsigned char *buff;
};

void __inline buffer_init(struct buffer *bf)
{
    bf->len = 0;
    bf->alloc = 0;
    bf->buff = NULL;
}

void __inline buffer_free(struct buffer *bf)
{
    if (bf->buff != NULL)
    {
        free(bf->buff);
        bf->buff = NULL;
    }
    bf->alloc = 0;
    bf->len = 0;
}

void __inline buffer_clear(struct buffer *bf)
{
    bf->len = 0;
}

void __inline buffer_swap(struct buffer *bf1, struct buffer *bf2)
{
    struct buffer bf;

    bf = *bf1;
    *bf1 = *bf2;
    *bf2 = bf;
}

BOOL __inline buffer_alloc(struct buffer *bf, int alloc)
{
    if (bf->alloc < alloc + 1)
    {
        int newAlloc = (alloc / 1024 + 1) * 1024;
        unsigned char *newBuff = (unsigned char *)malloc(newAlloc);
        if (newBuff == NULL) return FALSE;
        if (bf->buff != NULL)
        {
            if (bf->len)
                memcpy(newBuff, bf->buff, bf->len);
            free(bf->buff);
        }
        bf->buff = newBuff;
        bf->alloc = newAlloc;
    }
    return TRUE;
}

BOOL __inline buffer_expand(struct buffer *bf, int exp)
{
    return buffer_alloc(bf, bf->alloc + exp);
}

void __inline buffer_setData(struct buffer *bf, unsigned char *data, int dataLen)
{
    if (bf->buff != NULL)
        free(bf->buff);

    bf->buff = data;
    bf->alloc = bf->len = dataLen;
}

BOOL __inline buffer_assign(struct buffer *bf, const void *data, int dataLen)
{
    if (!buffer_alloc(bf, dataLen))
        return FALSE;

    memcpy(bf->buff, data, dataLen);
    bf->len = dataLen;
    return TRUE;
}

BOOL __inline buffer_append(struct buffer *bf, const void *data, int data_len)
{
    if (!buffer_alloc(bf, bf->len + data_len))
        return FALSE;

    memcpy(bf->buff + bf->len, data, data_len);
    bf->len += data_len;
    return TRUE;
}

void __inline buffer_erase(struct buffer *bf, int erase_pos, int erase_len)
{
    if (erase_pos >= 0 &&
        erase_pos < bf->len &&
        (erase_pos + erase_len) <= bf->len)
    {
        if ((erase_pos + erase_len) < bf->len)
            memmove(bf->buff + erase_pos,
                bf->buff + erase_pos + erase_len,
                bf->len - erase_pos - erase_len);
        bf->len -= erase_len;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* _BUFFER_H */
