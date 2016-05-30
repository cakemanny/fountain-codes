#ifndef __BITSET_H__
#define __BITSET_H__

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include "preheader.h" // define __has_builtin for non-clang

#define IsBitSet32(x, i) (( (x)[(i)>>5] & (1<<((i)&31)) ) != 0)
#define SetBit32(x, i) (x)[(i)>>5] |= (1<<((i)&31))
#define ClearBit32(x, i) (x)[(i)>>5] &= (1<<((i)&31)) ^ 0xFFFFFFFF


#define IsBitSet64(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit64(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit64(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL

typedef uint64_t* bset64;
typedef uint32_t* bset32;

#if defined(__x86_64__)
#   define BSET_BITS        64
#   define BSET_BITS_W      6
#   define PRIbset          PRIx64
#   define bset             bset64
#   define bset_int         uint64_t
#   if __has_builtin(__builtin_ctzll) || defined(__GNUC__)
#       define ctz              __builtin_ctzll
#       define HAVE_CTZ
#   endif
#   define IsBitSet(x, i)   IsBitSet64(x, i)
#   define SetBit(x, i)     SetBit64(x, i)
#   define ClearBit(x, i)   ClearBit64(x, i)
#else
#   define BSET_BITS        32
#   define BSET_BITS_W      5
#   define PRIbset          PRIx32
#   define bset             bset32
#   define bset_int         uint32_t
#   if __has_builtin(__builtin_ctz) || defined(__GNUC__)
#       define ctz              __builtin_ctz
#       define HAVE_CTZ
#   endif
#   define IsBitSet(x, i)   IsBitSet32(x, i)
#   define SetBit(x, i)     SetBit32(x, i)
#   define ClearBit(x, i)   ClearBit32(x, i)
#endif

static size_t bset_len(int num_bits) {
    return (num_bits + (BSET_BITS-1)) / BSET_BITS;
}

static bset bset_alloc(int num_bits) __malloc;
bset bset_alloc(int num_bits)
{
    int len = bset_len(num_bits);
    if (__builtin_popcount(len) == 1) { // power of 2
        int alignment = (len <= 2) ? len : 4;
        bset mem = NULL;
        posix_memalign((void**)&mem, alignment * sizeof(bset_int), len * sizeof(bset_int));
        if (mem)
            memset(mem, 0, len * sizeof(bset_int));
        return mem;
    } else {
        return calloc(len, sizeof(bset_int));
    }
}

static int issubset_bit(const bset sub, const bset super, size_t len)
{
    bset_int result = 0;
    for (int i = 0; i < len; i++) {
        result |= (sub[i] & super[i]) ^ sub[i];
    }
    return !result;
}

#endif // __BITSET_H__
