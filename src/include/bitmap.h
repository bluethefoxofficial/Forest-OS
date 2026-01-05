#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*
 * bitmap.h - Bit manipulation and bitmap operations for Forest OS
 *
 * Linux-compatible bitmap operations for managing bit arrays
 */

/* Bits per word depends on architecture */
#define BITS_PER_BYTE   8
#ifndef BITS_PER_LONG
#define BITS_PER_LONG   (sizeof(unsigned long) * BITS_PER_BYTE)
#endif

/* Calculate number of longs needed for a given number of bits */
#ifndef BITS_TO_LONGS
#define BITS_TO_LONGS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#endif

/* Declare a bitmap with the given name and number of bits */
#define DECLARE_BITMAP(name, bits) \
    unsigned long name[BITS_TO_LONGS(bits)]

/* Define a bitmap with initialization to zero */
#define DEFINE_BITMAP(name, bits) \
    unsigned long name[BITS_TO_LONGS(bits)] = { 0 }

/* Bit position helpers */
#define BIT_WORD(nr)        ((nr) / BITS_PER_LONG)
#define BIT_MASK(nr)        (1UL << ((nr) % BITS_PER_LONG))

/* Generic bit manipulation (non-atomic) */
static inline void __set_bit(unsigned long nr, volatile unsigned long *addr)
{
    addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}

static inline void __clear_bit(unsigned long nr, volatile unsigned long *addr)
{
    addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}

static inline void __change_bit(unsigned long nr, volatile unsigned long *addr)
{
    addr[BIT_WORD(nr)] ^= BIT_MASK(nr);
}

static inline int __test_bit(unsigned long nr, const volatile unsigned long *addr)
{
    return (addr[BIT_WORD(nr)] & BIT_MASK(nr)) != 0;
}

static inline int __test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
    unsigned long old = *p;
    *p = old | mask;
    return (old & mask) != 0;
}

static inline int __test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
    unsigned long old = *p;
    *p = old & ~mask;
    return (old & mask) != 0;
}

/* Atomic bit operations using lock prefix on x86 */
static inline void set_bit(unsigned long nr, volatile unsigned long *addr)
{
    __asm__ __volatile__(
        "lock bts %1, %0"
        : "+m" (*(volatile unsigned long *)addr)
        : "Ir" (nr)
        : "memory");
}

static inline void clear_bit(unsigned long nr, volatile unsigned long *addr)
{
    __asm__ __volatile__(
        "lock btr %1, %0"
        : "+m" (*(volatile unsigned long *)addr)
        : "Ir" (nr)
        : "memory");
}

static inline void change_bit(unsigned long nr, volatile unsigned long *addr)
{
    __asm__ __volatile__(
        "lock btc %1, %0"
        : "+m" (*(volatile unsigned long *)addr)
        : "Ir" (nr)
        : "memory");
}

static inline int test_bit(unsigned long nr, const volatile unsigned long *addr)
{
    return __test_bit(nr, addr);
}

static inline int test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
    int oldbit;
    __asm__ __volatile__(
        "lock bts %2, %1\n\t"
        "sbb %0, %0"
        : "=r" (oldbit), "+m" (*(volatile unsigned long *)addr)
        : "Ir" (nr)
        : "memory");
    return oldbit;
}

static inline int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
    int oldbit;
    __asm__ __volatile__(
        "lock btr %2, %1\n\t"
        "sbb %0, %0"
        : "=r" (oldbit), "+m" (*(volatile unsigned long *)addr)
        : "Ir" (nr)
        : "memory");
    return oldbit;
}

/* Bitmap operations */
static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
    unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
    memset(dst, 0, len);
}

static inline void bitmap_fill(unsigned long *dst, unsigned int nbits)
{
    unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
    memset(dst, 0xff, len);
}

static inline void bitmap_copy(unsigned long *dst, const unsigned long *src,
                               unsigned int nbits)
{
    unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
    memcpy(dst, src, len);
}

static inline void bitmap_and(unsigned long *dst, const unsigned long *src1,
                              const unsigned long *src2, unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        dst[k] = src1[k] & src2[k];
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *src1,
                             const unsigned long *src2, unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        dst[k] = src1[k] | src2[k];
}

static inline void bitmap_xor(unsigned long *dst, const unsigned long *src1,
                              const unsigned long *src2, unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        dst[k] = src1[k] ^ src2[k];
}

static inline void bitmap_complement(unsigned long *dst, const unsigned long *src,
                                     unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        dst[k] = ~src[k];
}

static inline bool bitmap_empty(const unsigned long *src, unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        if (src[k])
            return false;
    return true;
}

static inline bool bitmap_full(const unsigned long *src, unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        if (~src[k])
            return false;
    return true;
}

static inline bool bitmap_equal(const unsigned long *src1,
                                const unsigned long *src2, unsigned int nbits)
{
    unsigned int k, lim = BITS_TO_LONGS(nbits);
    for (k = 0; k < lim; k++)
        if (src1[k] != src2[k])
            return false;
    return true;
}

/* Find first set bit */
static inline unsigned long find_first_bit(const unsigned long *addr,
                                           unsigned long size)
{
    unsigned long idx;
    for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
        if (addr[idx]) {
            unsigned long bit;
            __asm__ __volatile__(
                "bsf %1, %0"
                : "=r" (bit)
                : "r" (addr[idx])
            );
            return idx * BITS_PER_LONG + bit;
        }
    }
    return size;
}

/* Find first zero bit */
static inline unsigned long find_first_zero_bit(const unsigned long *addr,
                                                unsigned long size)
{
    unsigned long idx;
    for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
        if (~addr[idx]) {
            unsigned long bit;
            __asm__ __volatile__(
                "bsf %1, %0"
                : "=r" (bit)
                : "r" (~addr[idx])
            );
            return idx * BITS_PER_LONG + bit;
        }
    }
    return size;
}

/* Find next set bit starting from offset */
static inline unsigned long find_next_bit(const unsigned long *addr,
                                          unsigned long size, unsigned long offset)
{
    unsigned long tmp;

    if (offset >= size)
        return size;

    tmp = addr[BIT_WORD(offset)];
    tmp &= ~0UL << (offset & (BITS_PER_LONG - 1));

    if (tmp)
        goto found_first;

    offset = (offset + BITS_PER_LONG) & ~(BITS_PER_LONG - 1);

    while (offset < size) {
        tmp = addr[BIT_WORD(offset)];
        if (tmp)
            goto found_middle;
        offset += BITS_PER_LONG;
    }

    return size;

found_first:
found_middle:
    {
        unsigned long bit;
        __asm__ __volatile__(
            "bsf %1, %0"
            : "=r" (bit)
            : "r" (tmp)
        );
        return offset + bit;
    }
}

/* Iterate over each set bit in a bitmap */
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit((addr), (size)); \
         (bit) < (size); \
         (bit) = find_next_bit((addr), (size), (bit) + 1))

#endif /* BITMAP_H */
