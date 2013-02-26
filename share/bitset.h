#ifndef _BITSET_H_
#define _BITSET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <malloc.h>

struct bitset
{
    UCHAR *bits;
    UINT32 bitCount;
    UINT32 byteCount;
};

__inline void bitset_init(struct bitset *b, UINT32 bitCount)
{
    if (!bitCount)
    {
        if (b->bits) free(b->bits);
        memset(b, 0, sizeof(struct bitset));
    }
    else
    {
        if (b->bitCount != bitCount)
        {
            if (b->bits) free(b->bits);
            b->bitCount = bitCount;
            b->byteCount = (bitCount + 7) / 8;
            b->bits = (UCHAR *)malloc(b->byteCount);
        }
        memset(b->bits, 0, b->byteCount);
    }
}

__inline void bitset_copy(struct bitset *dst, struct bitset *src)
{
    bitset_init(dst, src->bitCount);
    if (src->bits && src->byteCount)
        memcpy(dst->bits, src->bits, src->byteCount);
}

__inline void bitset_free(struct bitset *b)
{
    if (b)
    {
        if (b->bits) free(b->bits);
        b->bits = NULL;
        b->bitCount = 0;
        b->byteCount = 0;
    }
}

__inline int bitset_isEmpty(const struct bitset *b)
{
    UINT32 i;

    for (i = 0; i < b->byteCount; ++i)
        if (b->bits[i])
            return 0;

    return 1;
}

__inline int bitset_set(struct bitset *b, UINT32 nth)
{
    if (nth >= b->bitCount)
        return -1;

    b->bits[nth >> 3] |= (0x80 >> (nth & 7));
    return 0;
}

__inline void bitset_setAll(struct bitset *b)
{
    UINT32 nth;
    for (nth = 0; nth < b->bitCount; nth ++)
        b->bits[nth >> 3] |= (0x80 >> (nth & 7));
}

__inline int bitset_clear(struct bitset *bitset, UINT32 nth)
{
    if (nth >= bitset->bitCount)
        return -1;

    bitset->bits[nth >> 3] &= (0xff7f >> (nth & 7));
    return 0;
}

__inline void bitset_clearAll(struct bitset *b)
{
    memset(b->bits, 0, b->byteCount);
}

__inline int bitset_check(struct bitset *b, UINT32 nth)
{
    if (nth >= b->bitCount) return -1;

    return ((b->bits[nth >> 3] & (0x80 >> (nth & 7))) ? 1 : 0);
}

__inline UINT32 bitset_getNextTrue(struct bitset *b, UINT32 start, UINT32 end)
{
    UINT32 i;

    for (i = start; i < end; ++i)
        if (1 == bitset_check(b, i)) return i;

    return MAXUINT32;
}

__inline UINT32 bitset_getNextEmpty(struct bitset *b, UINT32 start, UINT32 end)
{
    UINT32 i;

    for (i = start; i < end; ++i)
        if (0 == bitset_check(b, i))
            return i;

    return MAXUINT32;
}

/* Sets bit range [begin, end) to 1 */
__inline int bitset_setRange(struct bitset * b, UINT32 begin, UINT32 end)
{
    UINT32 sb, eb;
    unsigned char sm, em;

    end--;

    if ((end >= b->bitCount) || (begin > end))
        return -1;

    sb = begin >> 3;
    sm = ~(0xff << (8 - (begin & 7)));
    eb = end >> 3;
    em = 0xff << (7 - (end & 7));

    if (sb == eb)
    {
        b->bits[sb] |= (sm & em);
    }
    else
    {
        b->bits[sb] |= sm;
        b->bits[eb] |= em;
        if (++sb < eb)
            memset (b->bits + sb, 0xff, eb - sb);
    }

    return 0;
}

/* Clears bit range [begin, end) to 0 */
__inline int bitset_clearRange(struct bitset * b, UINT32 begin, UINT32 end)
{
    UINT32 sb, eb;
    unsigned char sm, em;

    end--;

    if ((end >= b->bitCount) || (begin > end))
        return -1;

    sb = begin >> 3;
    sm = 0xff << (8 - (begin & 7));
    eb = end >> 3;
    em = ~(0xff << (7 - (end & 7)));

    if (sb == eb)
    {
        b->bits[sb] &= (sm | em);
    }
    else
    {
        b->bits[sb] &= sm;
        b->bits[eb] &= em;
        if (++sb < eb)
            memset (b->bits + sb, 0, eb - sb);
    }

    return 0;
}

__inline struct bitset *bitset_or(struct bitset * a, const struct bitset * b)
{
    UCHAR *ait = a->bits;
    UCHAR *aend = ait + a->byteCount;
    UCHAR *bit = b->bits;
    UCHAR *bend = bit + b->byteCount;

    while (ait != aend && bit != bend)
        *ait++ |= *bit++;

    return a;
}

/* set 'a' to all the flags that were in 'a' but not 'b' */
__inline void bitset_diff(struct bitset * a, const struct bitset * b)
{
    UCHAR *ait = a->bits;
    UCHAR *aend = ait + a->byteCount;
    UCHAR *bit = b->bits;
    UCHAR *bend = bit + b->byteCount;

    while (ait != aend && bit != bend)
        *ait++ &= ~(*bit++);
}

__inline UINT32 bitset_countTrueBits(const struct bitset* b)
{
    UINT32 ret = 0;
    const UCHAR *it, *end;
    static const int trueBitCount[256] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
    };

    if (!b)
        return 0;

    for (it = b->bits, end = it + b->byteCount; it != end; ++it)
        ret += trueBitCount[*it];

    return ret;
}

#ifdef __cplusplus
}
#endif

#endif
