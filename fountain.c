#define _GNU_SOURCE // asks stdio.h to include asprintf

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#ifdef __x86_64__
#   include <x86intrin.h>
#endif
#include "preheader.h" // define __has_builtin for non-clang
#include "errors.h"
#include "platform.h"
#include "fountain.h"
#include "dbg.h"
#include "randgen.h"
#include "bitset.h"

#define ISBITSET(x, i) (( (x)[(i)>>3] & (1<<((i)&7)) ) != 0)
#define SETBIT(x, i) (x)[(i)>>3] |= (1<<((i)&7))
#define CLEARBIT(x, i) (x)[(i)>>3] &= (1<<((i)&7)) ^ 0xFF

#define max(a,b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })
#define min(a,b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })

#define BUFFER_SIZE 256

char* memdecodestate_filename = "__memory__fountain__";


static char * xorncpy (char* destination, const char* source, register size_t n) {
    register char* d = destination;
    register const char* s = source;
    do {
        *d++ ^= *s++;
    } while (--n != 0);
    return (destination);
}

#ifndef HAVE_CTZ
/* We only use these if we don't has ffs */
static const char LogTable256[256] =
{
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    LT(4), LT(5), LT(5), LT(6), LT(6), LT(6), LT(6),
    LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)
};


static unsigned int log2i_32(unsigned int v) { // 32-bit word to find the log of
    register unsigned int t, tt; // temporaries
    if ((tt = v >> 16)) {
        return (t = tt >> 8) ? 24 + LogTable256[t] : 16 + LogTable256[tt];
    } else {
        return (t = v >> 8) ? 8 + LogTable256[t] : LogTable256[v];
    }
}

#ifdef __x86_64__
static uint64_t log2i_64(uint64_t v) {
    register uint64_t t;
    return ((t = v >> 32)) ? 32 + log2i_32(t) : log2i_32(v);
}
#   define log2i(x)     log2i_64(x)
#else
#   define log2i(x)     log2i_32(x)
#endif

#endif /*HAVE_CTZ*/

static int size_in_blocks(int string_len, int blk_size) {
    return (string_len + blk_size - 1) / blk_size;
}

/*
 * param n = filesize in blocks
 */
static int choose_num_blocks(const int n) {
    // Effectively uniform random double between 0 and 1
    double x = (double)rand() / (double)RAND_MAX;
    // Distribute to make smaller blocks more common
    double d = (double)n * (x <= 0.5 ? x*x*x : 1 - x*x*x);
    // Windows might pick rand()==RAND_MAX, so min these
    return min(1 + (int)floor(d), n);
}

/*
 * param n filesize in blocks
 * param d number of blocks in fountain/packet
 * param blocks The result
 */
static void seeded_select_blocks(int* blocks, int n, int d, uint64_t seed) {
    assert( d <= n );

    for (int i = 0; i < d; i++) {
        randgen_s gen = next_rand(seed);
        seed = gen.next_seed;

        blocks[i] = gen.result % n;
        for (int j = 0; j < i; j++) {
            if (blocks[i] == blocks[j]) {
                i--;
                break;
            }
        }
    }
}

/*
 * Same as seeded_select_blocks but creates a bitset rather than an array of
 * the block numbers
 */
static bset seeded_select_blockset(int n, int d, uint64_t seed) {
    assert( d <= n );
    bset block_set = bset_alloc(n);
    check_mem(block_set);

    for (int i = 0; i < d; i++) {
        randgen_s gen = next_rand(seed);
        seed = gen.next_seed;

        int block_num = gen.result % n;
        if (IsBitSet(block_set, block_num))
            --i;
        else
            SetBit(block_set, block_num);
    }
    return block_set;
error:
    return NULL;
}

/* makes a fountain fountain, given a file */
fountain_s* fmake_fountain(FILE* f, int blk_size, int section, int section_size) {
    static char fmake_buf[4096];

    int bytes_per_section = section_size * blk_size;
    int offset = section * bytes_per_section;

    fountain_s* output = malloc(sizeof *output);
    if (!output) return NULL;

    memset(output, 0, sizeof *output);

    output->blk_size = blk_size;
    output->section = section;

    int n = section_size; // Always
    output->num_blocks = choose_num_blocks(n);
    assert( output->num_blocks > 0 );
    output->seed = rand();
    int block_list[output->num_blocks];
    seeded_select_blocks(block_list, n, output->num_blocks, output->seed);

    // XOR blocks together
    // Why blk_size + 1? we are no longer terminate with NULL
    output->string = calloc(blk_size, sizeof *output->string);
    if (!output->string) goto free_ftn;

    // Only allocate if we actually need to
    char * buffer;
    if (blk_size > sizeof(fmake_buf)) {
         buffer = malloc(blk_size);
        if (!buffer) goto free_os;
    } else
        buffer = fmake_buf;

    for (int i = 0; i < output->num_blocks; i++) {
        int m = block_list[i] * blk_size;
        if (fseek(f, offset + m, SEEK_SET) < 0)  { /* m bytes from beginning of section */
            log_err("Couldn't seek to pos %d in file", offset + m);
            goto free_os;
        }
        size_t bytes = fread(buffer, 1, blk_size, f);
        if (bytes < blk_size && ferror(f)) {
            log_err("Error reading file");
            goto free_os;
        }
        if (bytes)
            xorncpy(output->string, buffer, bytes);
    }

    // Cleanup
    if (blk_size > sizeof(fmake_buf)) free(buffer);

    return output;

free_os:
free_ftn:
    free_fountain(output);
    return NULL;
}

fountain_s* make_fountain(const char* string, int blk_size, size_t length, int section) {
    fountain_s* output = malloc(sizeof *output);
    if (output == NULL) return NULL;

    memset(output, 0, sizeof *output);
    int n = size_in_blocks(length, blk_size);
    output->blk_size = blk_size;

    output->num_blocks = choose_num_blocks(n);
    output->seed = rand();

    int block_list[output->num_blocks];
    seeded_select_blocks(block_list, n, output->num_blocks, output->seed);

    // XOR blocks together
    output->string = calloc(blk_size, sizeof *output->string);
    if (!output->string) goto free_ftn;

    for (int i = 0; i < output->num_blocks; i++) {
        int m = block_list[i] * blk_size;
        xorncpy(output->string, string + m, blk_size);
    }

    // We need to allocate the blockset for our local test version
    output->block_set =
        seeded_select_blockset(n, output->num_blocks, output->seed);
    if (!output->block_set) goto free_ftn;
    output->block_set_len = bset_len(n);
    output->section = section;

    return output;

free_ftn:
    free_fountain(output);
    return NULL;
}

void free_fountain(fountain_s* ftn) {
    if (ftn->string) free(ftn->string);
    if (ftn->block_set) bset_free(ftn->block_set);
    free(ftn);
}

int cmp_fountain(fountain_s* ftn1, fountain_s* ftn2) {
    // We should never be comparing fountains from different files or sections
    assert(ftn1->section == ftn2->section);

    int ret;
    if (( (ret = ftn1->blk_size - ftn2->blk_size)
       || (ret = ftn1->num_blocks - ftn2->num_blocks)
       || (ret = memcmp(ftn1->string, ftn2->string, ftn1->blk_size)) ))
        return ret;

    for (int i=0; i < ftn1->block_set_len; ++i) {
        if (( ret = (ftn1->block_set[i] ^ ftn2->block_set[i]) ))
            return ret;
    }

    return 0;
}

int fountain_copy(fountain_s* dst, fountain_s* src) {
    int blk_size = (dst->blk_size = src->blk_size);
    dst->num_blocks = src->num_blocks;
    dst->section = src->section;
    dst->seed = src->seed;
    dst->block_set_len = src->block_set_len;

    dst->string = malloc(blk_size * sizeof *dst->string);
    if (!dst->string) goto cleanup;

    memcpy(dst->string, src->string, blk_size);

    dst->block_set = malloc(src->block_set_len * sizeof *dst->block_set);
    if (!dst->block_set) goto free_str;
    memcpy(dst->block_set, src->block_set, src->block_set_len * sizeof *dst->block_set);

    return 0;
free_str:
    free(dst->string);
cleanup:
    return ERR_MEM;
}

void print_fountain(const fountain_s * ftn) {
    printf("{ num_blocks: %"PRId32", blk_size: %"PRId16", section: %"PRIu16", seed: %"PRIu64", blocks: ",
            ftn->num_blocks, ftn->blk_size, ftn->section, ftn->seed);
    for (int i = 0; i < ftn->block_set_len; i++)
        printf("%"PRIbset, ftn->block_set[i]);
    printf("}\n");
}

/* Helper Methods for working with block_set */
// Assumes we know that there is exactly one bit set in the bitset
static int blockset_single_block_num(bset block_set) {
    int i = 0;
    while (!block_set[i]) i++;
#ifdef HAVE_CTZ /* Count-trailing-zeroes Will yield same result for our input */
    return i * BSET_BITS + ctz(block_set[i]);
#else
    return i * BSET_BITS + log2i(block_set[i]);
#endif // HAVE_CTZ
}

/*
 * finds the index of the next set bit starting from starting_index and
 * including starting_index.
 * This should give same result as while (!IsBitSet(block_set[j], j)) j++;
 */
static int blockset_lowest_set_above(bset block_set, int block_set_len, int starting_index) {
    bset_int j = starting_index;
#ifdef HAVE_CTZ
    bset_int k = j >> BSET_BITS_W; // Index of integer to check
    bset_int x = 1ULL << (j & (BSET_BITS-1));
    bset_int mask = x | ~(x - 1);
    while (!(block_set[k] & mask) && k < block_set_len) {
        k += 1;
        j = k << BSET_BITS_W;
        mask = ~0;
    }
    return (k < block_set_len)
        ? (j & ~(BSET_BITS-1)) + ctz(block_set[k] & mask)
        : -1;
#else
    while (!IsBitSet(block_set, j) && (j >> BSET_BITS_W) < block_set_len)
        j++;
    return (j >> BSET_BITS_W) < block_set_len ? j : -1;
#endif // HAVE_CTZ
}


// DEPRECATED
//static int fountain_issubset(const fountain_s* sub, const fountain_s* super) {
//    // A fast implementation based on a small number of shortcutting
//    // binary searches
//    int from = 0;
//    int to = super->num_blocks;
//    for (int i = 0; i < sub->num_blocks && from >= 0; i++) {
//        from = int_binary_search(super->block, from, to, sub->block[i]);
//    }
//    return (from >= 0);
//}

#ifdef __AVX__
typedef long long v4si __attribute__((vector_size (16)));
typedef long long v8si __attribute__((vector_size (32)));

static inline bool issubset_bit128(v4si sub, v4si super) {
    v4si result = (sub & super) ^ sub;
    //return (result[0] | result[1]) == 0;
    return _mm_testz_si128(result, result);
}
static inline bool issubset_bit256(v8si sub, v8si super) {
#ifdef __AVX2__
    v8si result = _mm256_andnot_si256(super, sub);
#else
    v8si result = (sub & super) ^ sub;
#endif
    //return (result[0] | result[1] | result[2] | result[3]) == 0;
    return _mm256_testz_si256(result, result);
}
#endif

#if defined(__x86_64__) && defined(__AVX__)
static inline bool issubset_bit512(const bset sub, const bset super)
{
    return
       issubset_bit256(*((v8si*)(sub    )), *((v8si*)(super)))
    && issubset_bit256(*((v8si*)(sub + 4)), *((v8si*)(super + 4)));
}

static inline bool issubset_bit1024(const bset sub, const bset super)
{
    return
       issubset_bit256(*((v8si*)(sub    )), *((v8si*)(super)))
    && issubset_bit256(*((v8si*)(sub + 4)), *((v8si*)(super + 4)))
    && issubset_bit256(*((v8si*)(sub + 8)), *((v8si*)(super + 8)))
    && issubset_bit256(*((v8si*)(sub +12)), *((v8si*)(super +12)));
}
#endif // __x86_64__ && __AVX__

static bool fountain_issubset_bit(const fountain_s* sub, const fountain_s* super) {
    assert( sub->section == super->section );
    assert( sub->block_set_len == super->block_set_len );
    // if we have a subset then sub[i] & super[i] == sub[i] forall i
    // so if result is still 0 at end then this is true
    switch (super->block_set_len) {
#if defined(__x86_64__) && defined(__AVX__)
        case 2:
            return issubset_bit128(*((v4si*)sub->block_set), *((v4si*)super->block_set));
        case 4: // We seem to be getting segfaults on this one
            return issubset_bit256(*((v8si*)sub->block_set), *((v8si*)super->block_set));
        case 8:
            return issubset_bit512(sub->block_set, super->block_set);
        case 16:
            return issubset_bit1024(sub->block_set, super->block_set);
#endif
        case 1:
            return (~*super->block_set & *sub->block_set) == 0;
    default:
        return issubset_bit(sub->block_set, super->block_set, super->block_set_len);
    }
}

/*
 * param sub the subset fountain used to reduce
 * param super the fountain to be reduced
 */
static void reduce_fountain(const fountain_s* sub, fountain_s* super) {
    // Here do the reduction
    // 1. xorncpy smaller into larger
    // 2. reallocate the actual block numbers
    // 3. decrement the number of blocks

    xorncpy(super->string, sub->string, sub->blk_size);
    super->num_blocks = super->num_blocks - sub->num_blocks;

    const int n = super->block_set_len;
    for (int i = 0; i < n; i++) {
        super->block_set[i] ^= sub->block_set[i];
    }
}

// This function can possibly be used instead of the 'Part 2' part for
// single blocks
static int reduce_against_hold(packethold_s* hold, fountain_s* ftn) {

    for (size_t i = 0; i < hold->offset; i++) {
        if (ISBITSET(hold->deleted, i))
            continue;

        fountain_s* from_hold = hold->fountain + i;

        if (from_hold->num_blocks == ftn->num_blocks)
            continue; // We are looking for strict subsets

        if (from_hold->num_blocks > ftn->num_blocks) {
            // Check if ftn is a subset of from_hold
            if (fountain_issubset_bit(ftn, from_hold)) {
                reduce_fountain(ftn, from_hold);

                SETBIT(hold->mark, i); // Mark the packet for retest after
            }
        } else {
            // Check if ftn is a superset of from_hold
            if (fountain_issubset_bit(from_hold, ftn)) {
                // Here reduce the ftn using the hold item, then send for a
                // retest
                reduce_fountain(from_hold, ftn);

                return 500; // RETEST -- need to define this
            }
        }
    }
    return 0; // No more to do
}


typedef int (*blockread_f)(void* /*buffer*/,
                            int /*blk_num*/,
                            decodestate_s* /*state*/);

typedef int (*blockwrite_f)(void* /*buffer*/,
                            int /*blk_num*/,
                            decodestate_s* /*state*/);

static int write_hold_ftn_to_output(
        decodestate_s* state,
        packethold_s* hold, int hold_offset, blockwrite_f bwrite);


static int _decode_fountain(decodestate_s* state, fountain_s* ftn,
        blockread_f bread, blockwrite_f bwrite) {
    assert(ftn->num_blocks > 0);

    bool retest = false;
    char* blkdec = state->blkdecoded;
    packethold_s* hold = state->hold;
    int blk_size = state->blk_size;

    //#ifndef NDEBUG
    //packethold_print(hold);
    //#endif

    do {
        retest = false;
        // Case one, block size one
        if (ftn->num_blocks == 1) {
            const int blk_num = blockset_single_block_num(ftn->block_set);
            assert( blk_num >= 0 );
            if (blkdec[blk_num] == 0) {
                if (bwrite(ftn->string, blk_num, state) != 1)
                    return ERR_BWRITE;
                blkdec[blk_num] = 1;
            } else { /* block already decoded */
                return F_ALREADY_DECODED;
            }

            // Part two check against blocks in hold
            for (int i = 0; i < hold->num_packets; i++) {
                if (ISBITSET(hold->deleted, i)) continue;
                fountain_s* hold_ftn = hold->fountain + i;

                if (fountain_issubset_bit(ftn, hold_ftn)) {
                    reduce_fountain(ftn, hold_ftn);

                    // On success check if hold packet is of size one block
                    if (hold_ftn->num_blocks == 1) {
                        int result = write_hold_ftn_to_output(state, hold,
                                                              i, bwrite);
                        if (result < 0)
                            return result;
                    }
                }
            }
            packethold_collect_garbage(hold);
        } else { /* size > 1, check against solved blocks */
            for (int i = 0, j = 0; i < ftn->num_blocks; i++) {
                j = blockset_lowest_set_above(
                        ftn->block_set, ftn->block_set_len, j);
                if (j == -1) break;
                if (blkdec[j]) {
                    // Xor the decoded block out of a new packet
                    char buf[blk_size];
                    memset(buf, 0, blk_size);
                    bread(buf, j, state);
                    xorncpy(ftn->string, buf, blk_size);

                    // Remove the decoded block number
                    ClearBit(ftn->block_set, j);

                    // reduce number of blocks held
                    ftn->num_blocks--;

                    // retest current reduced packet
                    retest = true;
                    break;
                }
                j++;
            }
            if (!retest) {
                odebug("%d", ftn->num_blocks);
                int result = reduce_against_hold(hold, ftn);
                if (result < 0) return result;
                if (result) {
                    retest = true;
                    odebug("%d after", ftn->num_blocks);
                    debug("Result = %d, doing retest", result);
                }
                for (int i = 0; i < hold->num_packets; i++) {
                    if (ISBITSET(hold->deleted, i))
                        continue;
                    if (ISBITSET(hold->mark, i)) {
                        // Could we use VPGATHERQQ to fetch the right packets?

                        if (hold->fountain[i].num_blocks == 1) {
                            int result = write_hold_ftn_to_output(state, hold,
                                                                  i, bwrite);
                            if (result < 0)
                                return result;
                        } else // Only if not removed
                            CLEARBIT(hold->mark, i);
                    }
                }
                packethold_collect_garbage(hold);
            }
        }
    } while (retest);
    if (ftn->num_blocks != 1) {
        bool inhold = false;
        for (size_t i = 0; i < hold->offset; i++) {
            if (cmp_fountain(ftn, hold->fountain + i) == 0) {
                inhold = true;
                break;
            }
        }
        if (!inhold) { /* Add packet to hold */
            if (packethold_add(hold, ftn) < 0)
                return handle_error(ERR_PACKET_ADD, NULL);
        }
    }
    return 0;
}

int write_hold_ftn_to_output(
        decodestate_s* state,
        packethold_s* hold,
        int hold_offset,
        blockwrite_f bwrite)
{
    int i = hold_offset;
    char* blkdec = state->blkdecoded;

    assert(!ISBITSET(hold->deleted, i));

    // move into output if we don't already have it
    fountain_s ltmp_ftn, *tmp_ftn;
    tmp_ftn = packethold_remove(hold, i, &ltmp_ftn);
    if (!tmp_ftn) return ERR_MEM;
    int tmp_bn = blockset_single_block_num(tmp_ftn->block_set);
    if (blkdec[tmp_bn] == 0) { /* not yet decoded so
                                            write to file */
        if (bwrite(tmp_ftn->string, tmp_bn, state) != 1) {
            free(tmp_ftn->string);
            bset_free(tmp_ftn->block_set);
            return ERR_BWRITE;
        }
        blkdec[tmp_bn] = 1;
        free(tmp_ftn->string);
        bset_free(tmp_ftn->block_set);
    }
    return 1;
}

/* the part that sets up the decodestate will be in client.c */

static int fblockwrite(void* buffer, int block, decodestate_s* state) {
    fseek(state->fp, block * state->blk_size, SEEK_SET);
    return fwrite(buffer, state->blk_size, 1, state->fp);
}

static int fblockread(void* buffer, int block, decodestate_s* state) {
    fseek(state->fp, block * state->blk_size, SEEK_SET);
    return fread(buffer, state->blk_size, 1, state->fp);
}


int fdecode_fountain(decodestate_s* state, fountain_s* ftn) {
    return _decode_fountain(state, ftn, &fblockread, &fblockwrite);
}

static int sblockread(void* buffer, int block, decodestate_s* state) {
   if (state->filename == memdecodestate_filename) {
       memdecodestate_s* mstate = (memdecodestate_s*) state;
       memcpy(buffer, mstate->result + (block * state->blk_size), state->blk_size);
       return 1;
   } else
       return -1;
}

static int sblockwrite(void * buffer, int block, decodestate_s* state) {
   if (state->filename == memdecodestate_filename) {
       memdecodestate_s* mstate = (memdecodestate_s*) state;
       memcpy(mstate->result + (block * state->blk_size), buffer, state->blk_size);
       return 1;
   } else
       return -1;
}
int memdecode_fountain(memdecodestate_s* state, fountain_s* ftn) {
    return _decode_fountain((decodestate_s*)state, ftn, &sblockread, &sblockwrite);
}

char* decode_fountain(const char* string, int blk_size) {
    int result = 0;

    int length = strlen(string);
    int num_blocks = size_in_blocks(length, blk_size);
    decodestate_s* state = decodestate_new(blk_size, num_blocks);
    if (!state) return NULL;

    /* Since we are using memory, attach a pointer to a result buffer */
    decodestate_s* tmp_ptr;
    tmp_ptr = realloc(state, sizeof(memdecodestate_s));
    if (tmp_ptr) state = tmp_ptr; else goto cleanup;

    //char* output = calloc(strlen(string) + 1, 1);
    char* output = calloc(num_blocks, blk_size);
    if (!output) goto cleanup;

    ((memdecodestate_s*)state)->result = output;

    state->filename = memdecodestate_filename;

    fountain_s* ftn = NULL;
    do {
        ftn = make_fountain(string, blk_size, length, 0);
        if (!ftn) goto cleanup;
        state->packets_so_far += 1;
        result = _decode_fountain(state, ftn, &sblockread, &sblockwrite);
        free_fountain(ftn);
        if (result < 0) goto cleanup;
    } while (!decodestate_is_decoded(state));

    decodestate_free(state);
    output = realloc(output, length + 1);
    output[length] = '\0';
    return output;

cleanup:
    decodestate_free(state);
    handle_error(result, "__memory__fountain__");
    return NULL;
}

/* Copied not quite verbatim from wikipedia.
   Used to check that network packets are intact
 */
static uint16_t Fletcher16(uint8_t const * data, int count)
{
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    for (int i = 0; i < count; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

static int fountain_packet_size(fountain_s* ftn) {
    return sizeof(uint16_t) // Make space for the checksum
        + FTN_HEADER_SIZE
        + ftn->blk_size;
    // don't transfer the block list
}

/* Serializes the sub-structures so that we can send it across the network
 */
buffer_s pack_fountain(fountain_s* ftn) {

    int packet_size = fountain_packet_size(ftn);
    void* buf_start = malloc(packet_size);
    if (!buf_start) return (buffer_s){.length=0, .buffer=NULL};

    uint16_t checksum = 0;

    // reference the memory after the checksum for convenience
    void* packed_ftn = buf_start + sizeof checksum;

    memcpy(packed_ftn, ftn, FTN_HEADER_SIZE);
    memcpy(packed_ftn + FTN_HEADER_SIZE, ftn->string, ftn->blk_size);

    // TODO: do byte order conversions

    // now we can calculate and fill in the hole at the beginning
    checksum = Fletcher16(packed_ftn, packet_size - sizeof checksum);
    memcpy(buf_start, &checksum, sizeof checksum);

    return (buffer_s) {
        .length = packet_size,
        .buffer = buf_start
    };
}


fountain_s* unpack_fountain(buffer_s packet, int section_size_in_blocks) {
    if (!packet.buffer) return NULL;

    uint16_t checksum = *((uint16_t*)packet.buffer);
// place the pointer passed the checksum to make the rest of the code in this
// function a tad more readble
    char const * packed_ftn = packet.buffer + sizeof checksum;

// because our fountain packet can be of variable size we had to wait until
// this point before we were able to calculate the checksum
    uint16_t calculated = Fletcher16((uint8_t*)packed_ftn, packet.length - sizeof checksum);
    odebug("%"PRIu16, checksum);
    odebug("%"PRIu16, calculated);
    if (checksum != calculated) {
        log_warn("checksums do not match");
        return NULL; }

    fountain_s* ftn = malloc(sizeof *ftn);
    if (!ftn)  return NULL;
    memcpy(ftn, packed_ftn, FTN_HEADER_SIZE);

    // TODO: do byte order conversions

    ftn->string = malloc(ftn->blk_size);
    if (!ftn->string) goto free_fountain;
    memcpy(ftn->string, packed_ftn + FTN_HEADER_SIZE, ftn->blk_size);

    ftn->block_set = seeded_select_blockset(section_size_in_blocks,
                                            ftn->num_blocks, ftn->seed);
    if (!ftn->block_set) goto free_string;
    ftn->block_set_len = bset_len(section_size_in_blocks);

    return ftn;
free_string:
    if (ftn->string) free(ftn->string);
free_fountain:
    free(ftn);
    // should we have a more resilient handler / a kinder one...
    return NULL;
}

/* ============ Packhold Functions ========================================= */

packethold_s* packethold_new() {
    packethold_s* hold = malloc(sizeof *hold);
    if (!hold) return NULL;
    memset(hold, 0, sizeof *hold);

    hold->num_slots = BUFFER_SIZE;

    hold->fountain = calloc(BUFFER_SIZE, sizeof *hold->fountain);
    if (!hold->fountain) goto free_hold;

    hold->mark = calloc((BUFFER_SIZE + 7) / 8, sizeof *hold->mark);
    if (!hold->mark) goto free_fountain;

    hold->deleted = calloc((BUFFER_SIZE + 7) / 8, sizeof *hold->deleted);
    if (!hold->deleted) goto free_mark;

    return hold;
free_mark:
    free(hold->mark);
free_fountain:
    free(hold->fountain);
free_hold:
    packethold_free(hold);
    return NULL;
}

void packethold_free(packethold_s* hold) {
    for (int i = 0; i < hold->num_packets; i++) {
        if (!ISBITSET(hold->deleted, i)) {
            if (hold->fountain[i].string) free(hold->fountain[i].string);
            if (hold->fountain[i].block_set) bset_free(hold->fountain[i].block_set);
        }
    }
    if (hold->mark) free(hold->mark);
    if (hold->deleted) free(hold->deleted);
    if (hold->fountain) free(hold->fountain);
    free(hold);
}

static int packethold_popcount(const packethold_s* hold)
{
    int num_deleted = 0;
    int len = (hold->num_packets + 7) / 8;
    for (int i = 0; i < len; i++) {
        num_deleted += __builtin_popcount((unsigned char)hold->deleted[i]);
    }
    return hold->num_packets - num_deleted;
}

/* Remove the ith item from the hold and return a copy of it */
fountain_s* packethold_remove(packethold_s* hold, int pos, fountain_s* output) {
    // trap double deletes
    assert(!ISBITSET(hold->deleted, pos));
    assert(pos >= 0 && pos < hold->num_packets);

    *output = hold->fountain[pos];
    hold->fountain[pos].string = NULL;
    hold->fountain[pos].block_set = NULL;

    debug("Setting pos %d as deleted", pos);
    SETBIT(hold->deleted, pos);
    CLEARBIT(hold->mark, pos); // safety

    // No longer do garbage collection here as this is called when looping over
    // the hold is happening
    return output;
}

void packethold_collect_garbage(packethold_s* hold)
{
#ifndef NDEBUG
    static int gc_count = 0;
#endif
    int popcount = packethold_popcount(hold);
    if (hold->offset <= 2 * popcount)
        return; // GC not needed

    debug("Collecting garbage: %d", ++gc_count);

    fountain_s* ftns = hold->fountain;

    char* deleted = hold->deleted;
    char* mark = hold->mark;
    int mp = 0; // mark position
    int i = 0;
    // Skip over stuff that is not deleted
    while (!ISBITSET(deleted, i)) { i++; mp++; };
    // back copy anything that is not deleted
    for (; i < hold->num_packets; i++) {
        if (!ISBITSET(deleted, i)) {
            ftns[mp] = ftns[i];
            if (ISBITSET(mark, i)) {
                SETBIT(mark, mp);
            } else {
                CLEARBIT(mark, mp);
            }
            mp++;
        }
    }
    assert(mp == popcount);
    hold->num_packets = hold->offset = popcount;

    memset(hold->deleted, 0, (hold->num_slots + 7) / 8);
}

int packethold_add(packethold_s* hold, fountain_s* ftn) {
    if (hold->offset >= hold->num_slots) {
        int space = hold->num_slots + (hold->num_slots >> 1);
        odebug("%d", space);

        fountain_s* tmp_ptr = hold->fountain;
        hold->fountain = realloc(hold->fountain, space * sizeof *hold->fountain);
        if (hold->fountain == NULL) {
            hold->fountain = tmp_ptr;
            return REALLOC_ERR;
        }

        char* mark_tmp_ptr = realloc(hold->mark, (space + 7) / 8);
        if (!mark_tmp_ptr) {
            handle_error(REALLOC_ERR, NULL);
            return REALLOC_ERR;
        } else {
            hold->mark = mark_tmp_ptr;
            int old_len = (hold->num_slots + 7) / 8;
            int new_len = (space + 7) / 8;
            memset(hold->mark  + old_len, 0, new_len - old_len);
        }

        char* deleted_tmp = realloc(hold->deleted, (space + 7) / 8);
        if (!deleted_tmp) {
            return handle_error(REALLOC_ERR, NULL);
        } else {
            hold->deleted = deleted_tmp;
            // initialize the new memory
            int old_len = (hold->num_slots + 7) / 8;
            int new_len = (space + 7) / 8;
            memset(hold->deleted + old_len, 0, new_len - old_len);
        }

        hold->num_slots = space;
    }

    //if (fountain_copy(&hold->fountain[hold->offset++], ftn) < 0)
    //    return ERR_MEM;
    // Shallow copy and null out the pointers since this is always the last
    // thing to happen before returning the packet
    hold->fountain[hold->offset++] = *ftn;
    ftn->string = NULL;
    ftn->block_set = NULL;

    CLEARBIT(hold->mark, hold->num_packets);
    hold->num_packets++;
    return 0;
}

void packethold_print(packethold_s* hold) {
    fprintf(stderr, "==== Packet Hold ====\n");
    fprintf(stderr, "\tnum_packets: %d\n\n", hold->num_packets);
    for (int i = 0; i < hold->num_packets; i++) {
        if (ISBITSET(hold->deleted, i)) // maybe we should print instead
            continue;
        fountain_s* ftn = hold->fountain + i;
        if (ISBITSET(hold->mark, i))
            fprintf(stderr, " *");
        else
            fprintf(stderr, "  ");
        for (int j = 0; j < ftn->block_set_len; j++)
            fprintf(stderr, "%"PRIbset, ftn->block_set[j]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "==== End P-Hold ====\n\n");
}

/* ============ Decode state Functions ===================================== */

/* State struct like

   int blk_size
   int num_blocks
   int* blkdecoded
   packethold_s* hold
   int packets_so_far
   char* filename
   FILE* fp
 */

decodestate_s* decodestate_new(int blk_size, int num_blocks) {
    decodestate_s* output = malloc(sizeof *output);
    if (!output) return NULL;

    *output = (decodestate_s) {
        .num_blocks = num_blocks,
        .packets_so_far = 0,
        .blk_size = blk_size
    };

    output->hold = packethold_new();
    if (!output->hold) goto cleanup;

    output->blkdecoded = calloc(num_blocks, sizeof *output->blkdecoded);
    if (!output->blkdecoded) goto free_hold;

    return output;

free_hold:
    packethold_free(output->hold);
cleanup:
    decodestate_free(output);
    return NULL;
}

void decodestate_free(decodestate_s* state) {
    if (state->blkdecoded)
        free(state->blkdecoded);
    if (state->hold)
        packethold_free(state->hold);
//    if (state->filename)
//        free(state->filename);
//    if (state->fp)
//        fclose(state->fp);
    free(state);
}

int decodestate_is_decoded(decodestate_s* state) {
    register char* dc = state->blkdecoded;
    register int num_solved = 0;
    for (int i = 0; i < state->num_blocks; i++) {
        num_solved += *dc++;
    }
    return (num_solved == state->num_blocks);
}

#ifdef UNIT_TESTS
int main(int argc, char** argv) {

    int i = 0;

    {
        bool passed = true;
        printf("Testing SetBit and IsBitSet...\n");
        for (i = 0; i < 4*BSET_BITS && passed; i++) {
            bset_int bitset[5] = {};
            SetBit(bitset, i);
            if (!IsBitSet(bitset, i))
                passed = false;
            ClearBit(bitset, i);
            if (IsBitSet(bitset, i))
                passed = false;
        }
        if (passed)
            printf("PASSED\n");
        else
            printf("FAILED: i = %d\n", i);
    }

    {
        bool passed = true;
        printf("Testing blockset_single_block_num...\n");
        for (i = 0; i < 4*BSET_BITS && passed; i++) {
            bset_int bitset[5] = {};
            SetBit(bitset, i);
            bset_int block_num = blockset_single_block_num(bitset);
            if (block_num != i)
                passed = false;
            ClearBit(bitset, i);
        }
        if (passed)
            printf("PASSED\n");
        else
            printf("FAILED: i = %d\n", i);
    }
    {
        bool passed = true;
        int j = 0, expected = 0, actual = 0;
        printf("Testing blockset_lowest_set_above...\n");
        for (i = 0; i < 4*BSET_BITS && passed; i+=7) {
            for (j = 0; j < 4*BSET_BITS && passed; j++) {
                bset_int bitset[5] = {};
                SetBit(bitset, i);

                expected = j;
                while (!IsBitSet(bitset, expected) && expected < 4*BSET_BITS)
                    expected++;
                if (expected == 4 * BSET_BITS) expected = -1;

                actual = blockset_lowest_set_above(bitset, 5, j);
                if (actual != expected)
                    passed = false;

                ClearBit(bitset, i);
            }
        }
        if (passed)
            printf("PASSED\n");
        else
            printf("FAILED: i = %d, j = %d, expected = %d, actual = %d\n",
                    i, j, expected, actual);
    }
}
#endif

