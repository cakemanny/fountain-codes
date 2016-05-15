#define _GNU_SOURCE // asks stdio.h to include asprintf

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>
#include "errors.h"
#include "platform.h"
#include "fountain.h"
#include "dbg.h"
#include "randgen.h"

#if !defined(__clang__) && !defined(__has_builtin)
#   if defined(__GNUC__)
#       define __has_builtin(x) 1
#   else
#       define __has_builtin(x) 0
#   endif
#endif

#define ISBITSET(x, i) (( (x)[(i)>>3] & (1<<((i)&7)) ) != 0)
#define SETBIT(x, i) (x)[(i)>>3] |= (1<<((i)&7))
#define CLEARBIT(x, i) (x)[(i)>>3] &= (1<<((i)&7)) ^ 0xFF

/* Int-based bitset? Use uint32_t */
#define IsBitSet(x, i) (( (x)[(i)>>5] & (1<<((i)&31)) ) != 0)
#define SetBit(x, i) (x)[(i)>>5] |= (1<<((i)&31))
#define ClearBit(x, i) (x)[(i)>>5] &= (1<<((i)&31)) ^ 0xFFFFFFFF

#define BUFFER_SIZE 256

#define MAX_BLOCK_SIZE 4096
static char fmake_buf[MAX_BLOCK_SIZE];

char* memdecodestate_filename = "__memory__fountain__";


static char * xorncpy (char* destination, const char* source, register size_t n) {
    register char* d = destination;
    register const char* s = source;
    do {
        *d++ ^= *s++;
    } while (--n != 0);
    return (destination);
}

#if !__has_builtin(__builtin_ctz) /* We only use these if we don't has ffs */
static const char LogTable256[256] =
{
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    LT(4), LT(5), LT(5), LT(6), LT(6), LT(6), LT(6),
    LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)
};


static unsigned int log2i(unsigned int v) { // 32-bit word to find the log of
    register unsigned int t, tt; // temporaries
    if ((tt = v >> 16)) {
        return (t = tt >> 8) ? 24 + LogTable256[t] : 16 + LogTable256[tt];
    } else {
        return (t = v >> 8) ? 8 + LogTable256[t] : LogTable256[v];
    }
}
#endif /*__builtin_ctz*/

static int size_in_blocks(int string_len, int blk_size) {
    return (string_len % blk_size)
        ? (string_len / blk_size) + 1 : string_len / blk_size;
}

/*
 * param n = filesize in blocks
 */
static int choose_num_blocks(const int n) {
    // Effectively uniform random double between 0 and 1
    double x = (double)rand() / (double)RAND_MAX;
    // Distribute to make smaller blocks more common
    double d = (double)n * (x <= 0.5 ? x*x*x : 1 - x*x*x);
    return (int) ceil(d);
}

/*
 * param n filesize in blocks
 * param d number of blocks in fountain/packet
 * param blocks The result
 */
static void seeded_select_blocks(int* blocks, int n, int d, uint64_t seed) {

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
 * @param blocks Not used - TODO: remove parameter
 */
static uint32_t* seeded_select_blockset(int* blocks, int n, int d, uint64_t seed) {
    uint32_t* block_set = calloc((n/32 + 1), sizeof *block_set);
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
fountain_s* fmake_fountain(FILE* f, int blk_size) {
    fountain_s* output = malloc(sizeof *output);
    if (!output) return NULL;

    memset(output, 0, sizeof *output);
    // get filesize
    fseek(f, 0, SEEK_END);
    int filesize = ftell(f);
    int n = size_in_blocks(filesize, blk_size);
    output->blk_size = blk_size;

    output->num_blocks = choose_num_blocks(n);
    output->seed = rand();
    int block_list[output->num_blocks];
    seeded_select_blocks(block_list, n, output->num_blocks, output->seed);

    // XOR blocks together
    // Why blk_size + 1? we are no longer terminate with NULL
    output->string = calloc(blk_size, sizeof *output->string);
    if (!output->string) goto free_ftn;

    // Only allocate if we actually need to
    char * buffer;
    if (blk_size <= MAX_BLOCK_SIZE) {
         buffer = malloc(blk_size);
        if (!buffer) goto free_os;
    } else
        buffer = fmake_buf;

    for (int i = 0; i < output->num_blocks; i++) {
        int m = block_list[i] * blk_size;
        fseek(f, m, SEEK_SET); /* m bytes from beginning of file */
        int bytes = fread(buffer, 1, blk_size, f);
        xorncpy(output->string, buffer, bytes);
    }

    // Cleanup
    if (blk_size <= MAX_BLOCK_SIZE) free(buffer);

    return output;

free_os:
free_ftn:
    free_fountain(output);
    return NULL;
}

fountain_s* make_fountain(const char* string, int blk_size, size_t length) {
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
        seeded_select_blockset(NULL, n, output->num_blocks, output->seed);
    if (!output->block_set) goto free_ftn;
    output->block_set_len = n/32 + 1;

    return output;

free_ftn:
    free_fountain(output);
    return NULL;
}

void free_fountain(fountain_s* ftn) {
    if (ftn->string) free(ftn->string);
    if (ftn->block_set) free(ftn->block_set);
    free(ftn);
}

int cmp_fountain(fountain_s* ftn1, fountain_s* ftn2) {
    int ret;
    if (( ret = ftn1->blk_size - ftn2->blk_size ))
        return ret;
    if (( ret = (ftn1->num_blocks - ftn2->num_blocks) ))
        return ret;
    if (( ret = memcmp(ftn1->string, ftn2->string, ftn1->blk_size) ))
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
    printf("{ num_blocks: %"PRId32", blk_size: %"PRId32", seed: %"PRIu64", blocks: ",
            ftn->num_blocks, ftn->blk_size, ftn->seed);
    for (int i = 0; i < ftn->block_set_len; i++)
        printf("%x", ftn->block_set[i]);
    printf("}\n");
}

/* Helper Methods for working with block_set */
// Assumes we know that there is exactly one bit set in the bitset
static int blockset_single_block_num(uint32_t* block_set) {
    int i = 0;
    while (!block_set[i]) i++;
#if __has_builtin(__builtin_ctz) /* Count-trailing-zeroes
                                    Will yield same result for our input */
    return i * 32 + __builtin_ctz(block_set[i]);
#else
    return i * 32 + log2i(block_set[i]);
#endif
}

/*
 * finds the index of the next set bit starting from starting_index and
 * including starting_index.
 * This should give same result as while (!IsBitSet(block_set[j], j)) j++;
 */
static int blockset_lowest_set_above(uint32_t* block_set, int block_set_len, int starting_index) {
    int j = starting_index;
#if __has_builtin(__builtin_ctz)
    int k = j>>5; // Index of integer to check
    int x = 1<<(j&31);
    int mask = x | ~(x - 1);
    while (!(block_set[k] & mask) && k < block_set_len) {
        k += 1;
        j = k<<5;
        mask = ~0;
    }
    return (k < block_set_len)
        ? (j & ~31) + __builtin_ctz(block_set[k] & mask)
        : -1;
#else
    while (!IsBitSet(block_set, j) && (j>>5) < block_set_len)
        j++;
    return (j<<5) < block_set_len ? j : -1;
#endif
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

static bool fountain_issubset_bit(const fountain_s* sub, const fountain_s* super) {
    // if we have a subset then sub[i] & super[i] == sub[i] forall i
    // so if result is still 0 at end then this is true
    uint32_t result = 0;
    int len = super->block_set_len;
    for (int i = 0; i < len; i++) {
        result |= ((sub->block_set[i] & super->block_set[i]) ^ sub->block_set[i]);
    }
    return !result;
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

static int _decode_fountain(decodestate_s* state, fountain_s* ftn,
        blockread_f bread, blockwrite_f bwrite) {
    if (ftn->num_blocks <= 0) // Could be a glitch
        return ERR_INVALID;

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
            if (blkdec[blk_num] == 0) {
                if (bwrite(ftn->string, blk_num, state) != 1)
                    return ERR_BWRITE;
                blkdec[blk_num] = 1;
            } else { /* block already decoded */
                return F_ALREADY_DECODED;
            }

            // Part two check against blocks in hold
            for (int i = 0; i < hold->num_packets; i++) {
                fountain_s* hold_ftn = hold->fountain + i;

                if (fountain_issubset_bit(ftn, hold_ftn)) {
                    reduce_fountain(ftn, hold_ftn);

                    // On success check if hold packet is of size one block
                    if (hold_ftn->num_blocks == 1) {
                        // move into output if we don't already have it
                        fountain_s ltmp_ftn, *tmp_ftn;
                        tmp_ftn = packethold_remove(hold, i, &ltmp_ftn);
                        int tmp_bn = blockset_single_block_num(tmp_ftn->block_set);
                        if (!tmp_ftn) return ERR_MEM;
                        if (blkdec[tmp_bn] == 0) { /* not yet decoded so
                                                                write to file */
                            if (bwrite(tmp_ftn->string, tmp_bn, state) != 1) {
                                free(tmp_ftn->string);
                                free(tmp_ftn->block_set);
                                return ERR_BWRITE;
                            }
                            blkdec[tmp_bn] = 1;
                            free(tmp_ftn->string);
                            free(tmp_ftn->block_set);
                        }
                        i--; // Since i now points to the next item on
                    }
                }
            }
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
                    if (ISBITSET(hold->mark, i)) {
                        // *** This is repeated from above >>
                        // TODO factor this out as a function
                        if (hold->fountain[i].num_blocks == 1) {
                            // move into output if we don't already have it
                            fountain_s ltmp_ftn, *tmp_ftn;
                            tmp_ftn = packethold_remove(hold, i, &ltmp_ftn);
                            if (!tmp_ftn) return ERR_MEM;
                            int tmp_bn = blockset_single_block_num(tmp_ftn->block_set);
                            if (blkdec[tmp_bn] == 0) { /* not yet decoded so
                                                                    write to file */
                                if (bwrite(tmp_ftn->string, tmp_bn, state) != 1) {
                                    free(tmp_ftn->string);
                                    free(tmp_ftn->block_set);
                                    return ERR_BWRITE;
                                }
                                blkdec[tmp_bn] = 1;
                                free(tmp_ftn->string);
                                free(tmp_ftn->block_set);
                            }
                            i--; /* Now points to the next item */
                        } else // Only if not removed
                            CLEARBIT(hold->mark, i);
                    }
                }
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
        ftn = make_fountain(string, blk_size, length);
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

    // now we can calculate and fill in the hole at the beginning
    checksum = Fletcher16(packed_ftn, packet_size - sizeof checksum);
    memcpy(buf_start, &checksum, sizeof checksum);

    return (buffer_s) {
        .length = packet_size,
        .buffer = buf_start
    };
}


fountain_s* unpack_fountain(buffer_s packet, int filesize_in_blocks) {
    if (!packet.buffer) return NULL;

    uint16_t checksum = *((uint16_t*)packet.buffer);
// place the pointer passed the checksum to make the rest of the code in this
// function a tad more readble
    char const * packed_ftn = packet.buffer + sizeof checksum;

// because our fountain packet can be of variable size we had to wait until
// this point before we were able to calculate the checksum
    int calculated = Fletcher16((uint8_t*)packed_ftn, packet.length - sizeof checksum);
    odebug("%d", checksum);
    odebug("%d", calculated);
    if (checksum != calculated) {
        log_warn("checksums do not match");
        return NULL; }

    fountain_s* ftn = malloc(sizeof *ftn);
    if (!ftn)  return NULL;
    memcpy(ftn, packed_ftn, FTN_HEADER_SIZE);

    ftn->string = malloc(ftn->blk_size);
    if (!ftn->string) goto free_fountain;
    memcpy(ftn->string, packed_ftn + FTN_HEADER_SIZE, ftn->blk_size);

    ftn->block_set = seeded_select_blockset(NULL,
            filesize_in_blocks, ftn->num_blocks, ftn->seed);
    if (!ftn->block_set) goto free_string;
    ftn->block_set_len = filesize_in_blocks/32 + 1;

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

    packethold_s tmp = {.num_slots=BUFFER_SIZE, .num_packets=0, .offset=0};
    *hold = tmp;

    hold->fountain = calloc(BUFFER_SIZE, sizeof *hold->fountain);
    if (!hold->fountain) goto free_hold;

    hold->mark = calloc(BUFFER_SIZE/8 + 1, sizeof(char));
    if (!hold->mark) goto free_fountain;

    return hold;
free_fountain:
    free(hold->fountain);
    hold->fountain = NULL;
free_hold:
    packethold_free(hold);
    return NULL;
}

void packethold_free(packethold_s* hold) {
    for (int i = 0; i < hold->num_packets; i++) {
        if (hold->fountain[i].string) free(hold->fountain[i].string);
        if (hold->fountain[i].block_set) free(hold->fountain[i].block_set);
    }
    if (hold->mark) free(hold->mark);
    if (hold->fountain) free(hold->fountain);
    free(hold);
}

/* Remove the ith item from the hold and return a copy of it */
fountain_s* packethold_remove(packethold_s* hold, int pos, fountain_s* output) {
    *output = hold->fountain[pos];

    char* mark = hold->mark;
    for (int j = pos; j < hold->num_packets - 1; j++) {
        hold->fountain[j] = hold->fountain[j+1];
        if (ISBITSET(mark, j + 1)) SETBIT(mark, j);
        else CLEARBIT(mark, j);
    }
    memset(hold->fountain + hold->offset - 1, 0, sizeof *hold->fountain);
    CLEARBIT(mark, hold->num_packets);
    hold->offset--;
    hold->num_packets--;

    /* Check that our packhold is not overly large */
    if (hold->num_slots > 2 * hold->offset && hold->num_slots > BUFFER_SIZE) {
        debug("reducing packethold size");
        odebug("%d", hold->num_packets);
        odebug(IFWIN32("%Iu","%zu"), hold->offset);
        odebug("%d", hold->num_slots);

        fountain_s* tmp_ptr = realloc(hold->fountain,
                hold->num_packets * sizeof *hold->fountain);
        if (tmp_ptr) hold->fountain = tmp_ptr;
        else {
            handle_error(REALLOC_ERR, NULL);
            return NULL;
        }

        char* mark_tmp_ptr = realloc(hold->mark, hold->num_packets/8 + 1);
        if (mark_tmp_ptr) hold->mark = mark_tmp_ptr;
        else {
            handle_error(REALLOC_ERR, NULL);
            return NULL;
        }

        hold->num_slots = hold->num_packets;
    }

    return output;
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

        char* mark_tmp_ptr = realloc(hold->mark, space/8 + 1);
        if (!mark_tmp_ptr) {
            handle_error(REALLOC_ERR, NULL);
            return REALLOC_ERR;
        } else
            hold->mark = mark_tmp_ptr;

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
        fountain_s* ftn = hold->fountain + i;
        if (ISBITSET(hold->mark, i))
            fprintf(stderr, " *");
        else
            fprintf(stderr, "  ");
        for (int j = 0; j < ftn->block_set_len; j++)
            fprintf(stderr, "%x", ftn->block_set[j]);
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
        for (i = 0; i < 4*32 && passed; i++) {
            uint32_t bitset[5] = {};
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
        for (i = 0; i < 4*32 && passed; i++) {
            uint32_t bitset[5] = {};
            SetBit(bitset, i);
            uint32_t block_num = blockset_single_block_num(bitset);
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
        for (i = 0; i < 4*32 && passed; i+=7) {
            for (j = 0; j < 4*32 && passed; j++) {
                uint32_t bitset[5] = {};
                SetBit(bitset, i);

                expected = j;
                while (!IsBitSet(bitset, expected) && expected < 4*32)
                    expected++;
                if (expected == 4 * 32) expected = -1;

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

