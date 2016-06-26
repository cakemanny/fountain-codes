#ifndef __BITSET_H__
#define __BITSET_H__

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#ifdef _WIN32
#   include <malloc.h>
#endif
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

static size_t bset_len(int num_bits) __attribute__((const));
size_t bset_len(int num_bits) {
    return (num_bits + (BSET_BITS-1)) / BSET_BITS;
}

static bset bset_alloc(int num_bits) __malloc;
static bset bset_alloc_many(int num_bits, int count) __malloc;
bset bset_alloc(int num_bits)
{
    return bset_alloc_many(num_bits, 1);
}
bset bset_alloc_many(int num_bits, int count)
{
    int len = bset_len(num_bits); // number of ints / int64s
    /*
     * If we are to use 256-bit SSE functions and registers then we need our
     * memory to be 32-byte aligned
     */
    if (__builtin_popcount(len) == 1) { // power of 2
        int width_in_bytes = len * sizeof(bset_int);
        int alignment = width_in_bytes > 32 ? 32 : width_in_bytes;

#ifdef _WIN32
        bset mem = _aligned_malloc(count * len * sizeof(bset_int), alignment);
#else
        bset mem = NULL;
        posix_memalign((void**)&mem, alignment, count * len * sizeof(bset_int));
#endif
        if (mem)
            memset(mem, 0, count * len * sizeof(bset_int));
        return mem;
    } else {
        return calloc(len, sizeof(bset_int));
    }
}
static void bset_free(bset bitset)
{
#ifdef _WIN32
    _aligned_free(bitset);
#else
    free(bitset);
#endif
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
