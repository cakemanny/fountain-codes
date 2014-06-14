#define _GNU_SOURCE // asks stdio.h to include asprintf

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "errors.h"
#include "fountain.h"
#include "dbg.h"
#include "randgen.h"

#define ISBITSET(x, i) (( (x)[i>>3] & (1<<((i)&7)) ) != 0)
#define SETBIT(x, i) (x)[(i)>>3] |= (1<<((i)&7))
#define CLEARBIT(x, i) (x)[(i)>>3] &= (1<<((i)&7)) ^ 0xFF

#define BUFFER_SIZE 256

static char * xorncpy (char* destination, const char* source, register size_t n) {
    register char* d = destination;
    register const char* s = source;
    do {
        *d++ ^= *s++;
    } while (--n != 0);
    return (destination);
}


static int size_in_blocks(const char* string, int blk_size) {
    int string_len = strlen(string);
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
    double d = (double)n * x * x;
    return (int) ceil(d);
}

static int int_compare(const void* a, const void* b) {
    return ( *(int*)a - *(int*)b );
}

/* This is going to replace the above shortly
 *
 * param n filesize in blocks
 * param d number of blocks in fountain/packet
 */
static int* seeded_select_blocks(int n, int d, unsigned long seed) {

    int* blocks = malloc(d * sizeof *blocks);
    if (!blocks) return NULL;

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
    qsort(blocks, d, sizeof *blocks, int_compare);

// Would be good to return the new seed aswell...
    return blocks;
}

/* makes a fountain fountain, given a file */
fountain_s* fmake_fountain(FILE* f, int blk_size) {
    fountain_s* output = malloc(sizeof *output);
    if (!output) return NULL;

    memset(output, 0, sizeof *output);
    // get filesize
    fseek(f, 0, SEEK_END);
    int filesize = ftell(f);
    int n = (filesize % blk_size)
        ? (filesize /blk_size) + 1 : filesize / blk_size;
    output->blk_size = blk_size;

    output->num_blocks = choose_num_blocks(n);
    output->seed = rand();
    output->block = seeded_select_blocks(n, output->num_blocks, output->seed);
    if (!output->block)
        goto free_ftn;

    // XOR blocks together
    // Why blk_size + 1? we are no longer terminate with NULL
    output->string = calloc(blk_size + 1, sizeof *output->string);
    if (!output->string) goto free_ob;

    char * buffer = malloc(blk_size);
    if (!buffer) goto free_os;

    for (int i = 0; i < output->num_blocks; i++) {
        int m = output->block[i] * blk_size;
        fseek(f, m, SEEK_SET); /* m bytes from beginning of file */
        int bytes = fread(buffer, 1, blk_size, f);
        xorncpy(output->string, buffer, bytes);
    }

    // Cleanup
    free(buffer);

    return output;

free_os:
free_ob:
free_ftn:
    free_fountain(output);
    return NULL;
}

fountain_s* make_fountain(const char* string, int blk_size) {
    fountain_s* output = malloc(sizeof *output);
    if (output == NULL) return NULL;

    memset(output, 0, sizeof *output);
    int n = size_in_blocks(string, blk_size);
    output->blk_size = blk_size;

    output->num_blocks = choose_num_blocks(n);
    output->seed = rand();
    output->block = seeded_select_blocks(n, output->num_blocks, output->seed);
    if (!output->block)
        goto free_ob;

    // XOR blocks together
    output->string = calloc(blk_size+1, sizeof *output->string);
    if (!output->string) goto free_os;

    for (int i = 0; i < output->num_blocks; i++) {
        int m = output->block[i] * blk_size;
        xorncpy(output->string, string + m, blk_size);
    }

    return output;

free_os:
free_ob:
    free_fountain(output);
    return NULL;
}

void free_fountain(fountain_s* ftn) {
    if (ftn->block) free(ftn->block);
    if (ftn->string) free(ftn->string);
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

    for (int i=0; i < ftn1->num_blocks; ++i) {
        if (( ret = (ftn1->block[i] - ftn2->block[i]) ))
            return ret;
    }

    return 0;
}

int fountain_copy(fountain_s* dst, fountain_s* src) {
    int blk_size = (dst->blk_size = src->blk_size);
    dst->num_blocks = src->num_blocks;
    dst->seed = src->seed;

    dst->string = malloc(blk_size * sizeof *dst->string);
    if (!dst->string) goto cleanup;

    memcpy(dst->string, src->string, blk_size);

    dst->block = malloc(src->num_blocks * sizeof *dst->block);
    if (!dst->block) goto free_str;

    memcpy(dst->block, src->block,src->num_blocks * sizeof *dst->block);

    return 0;
free_str:
    free(dst->string);
cleanup:
    return ERR_MEM;
}

void print_fountain(const fountain_s * ftn) {
    printf("{ num_blocks: %d, blk_size: %d, seed: %lu, blocks: ",
            ftn->num_blocks, ftn->blk_size, ftn->seed);
    for (int i = 0; i < ftn->num_blocks; i++) {
        printf("%d ", ftn->block[i]);
    }
    printf("}\n");
}

static int fountain_issubset(const fountain_s* sub, const fountain_s* super) {
    // We use the fact that the block list is ordered to create
    // a faster check O(n)
    int j = 0;
    for (int i = 0; i < super->num_blocks; i++) {
        if (super->block[i] == sub->block[j])
            j++;
    }
    return (j == sub->num_blocks);
}

/*
 * param sub the subset fountain used to reduce
 * param super the fountain to be reduced
 */
static int reduce_fountain(const fountain_s* sub, fountain_s* super) {
    // Here do the reduction
    // 1. xorncpy smaller into larger
    // 2. reallocate the actual block numbers
    // 3. decrement the number of blocks

    xorncpy(super->string, sub->string, sub->blk_size);
    int new_num_blocks = super->num_blocks - sub->num_blocks;
    int new_blocks[new_num_blocks];

    for (int i = 0, j = 0, k = 0; i < super->num_blocks; i++) {
        if (super->block[i] == sub->block[j])
            j++;
        else
            new_blocks[k++] = super->block[i];
    }
    // don't bother with realloc super->block, this is always a downsize

    memcpy(super->block, new_blocks, new_num_blocks * sizeof(int));
    super->num_blocks = new_num_blocks;
    return 0;
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
            if (fountain_issubset(ftn, from_hold)) {
                if (reduce_fountain(ftn, from_hold) < 0)
                    return ERR_MEM;

                SETBIT(hold->mark, i); // Mark the packet hold for retest after
            }
        } else {
            // Check if ftn is a superset of from_hold
            if (fountain_issubset(from_hold, ftn)) {
                // Here reduce the ftn using the hold item, then send for a
                // retest
                if (reduce_fountain(from_hold, ftn) < 0)
                    return ERR_MEM;

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

typedef int bool;
static bool const false = 0;
static bool const true = 1;

static int _decode_fountain(decodestate_s* state, fountain_s* ftn,
        blockread_f bread, blockwrite_f bwrite) {
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
            if (blkdec[ftn->block[0]] == 0) {
                if (bwrite(ftn->string, ftn->block[0], state) != 1)
                    return ERR_BWRITE;
                blkdec[ftn->block[0]] = 1;
            } else { /* block already decoded */
                return F_ALREADY_DECODED;
            }

            // Part two check against blocks in hold
            bool match = false;
            for (int i = 0; i < hold->num_packets; i++) {
                for (int j = 0; j < hold->fountain[i].num_blocks; j++) {
                    if (hold->fountain[i].block[j] == ftn->block[0]) {
                        // Xor out the hold block
                        xorncpy(hold->fountain[i].string, ftn->string, blk_size);

                        // Remove removed blk number
                        for (int k = i; k < hold->fountain[i].num_blocks-1; k++) {
                            hold->fountain[i].block[k] =
                                hold->fountain[i].block[k+1];
                        }
                        int k = hold->fountain[i].num_blocks - 1;
                        hold->fountain[i].block[k] = 0;
                        hold->fountain[i].num_blocks--;
                        match = true;
                        break;
                    }
                }
                if (!match) continue;
                // On success check if hold packet is of size one block
                if (hold->fountain[i].num_blocks == 1) {
                    // move into output if we don't already have it
                    fountain_s* tmp_ftn = packethold_remove(hold, i);
                    if (!tmp_ftn) return ERR_MEM;
                    if (blkdec[tmp_ftn->block[0]] == 0) { /* not yet decoded so
                                                            write to file */
                        if (bwrite(tmp_ftn->string,
                                tmp_ftn->block[0],
                                state) != 1) {
                            free_fountain(tmp_ftn);
                            return ERR_BWRITE;
                        }
                        blkdec[tmp_ftn->block[0]] = 1;
                    }
                    free_fountain(tmp_ftn);
                }
            }
        } else { /* size > 1, check against solved blocks */
            for (int i = 0; i < ftn->num_blocks; i++) {
                if (blkdec[ftn->block[i]]) {
                    // Xor the decoded block out of a new packet
                    char buf[blk_size];
                    memset(buf, 0, blk_size);
                    bread(buf, ftn->block[i], state);
                    xorncpy(ftn->string, buf, blk_size);

                    // Remove the decoded block number
                    for (int j = i; j < ftn->num_blocks - 1; j++) {
                        ftn->block[j] = ftn->block[j + 1];
                    }
                    ftn->block[ftn->num_blocks - 1] = 0;

                    // reduce number of blocks held
                    ftn->num_blocks--;

                    // retest current reduced packet
                    retest = true;
                    break;
                }
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
                            fountain_s* tmp_ftn = packethold_remove(hold, i);
                            if (!tmp_ftn) return ERR_MEM;
                            if (blkdec[tmp_ftn->block[0]] == 0) { /* not yet decoded so
                                                                    write to file */
                                if (bwrite(tmp_ftn->string,
                                        tmp_ftn->block[0],
                                        state) != 1) {
                                    free_fountain(tmp_ftn);
                                    return ERR_BWRITE;
                                }
                                blkdec[tmp_ftn->block[0]] = 1;
                            }
                            free_fountain(tmp_ftn);

                        }
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
   if (strcmp(state->filename, "__memory__fountain__") == 0) {
       memdecodestate_s* mstate = (memdecodestate_s*) state;
       memcpy(buffer, mstate->result + (block * state->blk_size), state->blk_size);
       return 1;
   } else
       return -1;
}

static int sblockwrite(void * buffer, int block, decodestate_s* state) {
   if (strcmp(state->filename, "__memory__fountain__") == 0) {
       memdecodestate_s* mstate = (memdecodestate_s*) state;
       memcpy(mstate->result + (block * state->blk_size), buffer, state->blk_size);
       return 1;
   } else
       return -1;
}

char* decode_fountain(const char* string, int blk_size) {
    int result = 0;

    int num_blocks = size_in_blocks(string, blk_size);
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

    state->filename = "__memory__fountain__";

    fountain_s* ftn = NULL;
    do {
        ftn = make_fountain(string, blk_size);
        if (!ftn) goto cleanup;
        state->packets_so_far += 1;
        result = _decode_fountain(state, ftn, &sblockread, &sblockwrite);
        free_fountain(ftn);
        if (result < 0) goto cleanup;
    } while (!decodestate_is_decoded(state));

    decodestate_free(state);
    output = realloc(output, strlen(string) + 1);
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
        + sizeof *ftn
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

    memcpy(packed_ftn, ftn, sizeof *ftn);
    memcpy(packed_ftn + sizeof *ftn, ftn->string, ftn->blk_size);

    fountain_s* f_ptr = (fountain_s*) packed_ftn;
    f_ptr->string = packed_ftn + sizeof *ftn;
    f_ptr->block = 0;

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
    memcpy(ftn, packed_ftn, sizeof *ftn);


    ftn->string = malloc(ftn->blk_size);
    if (!ftn->string) goto free_fountain;
    memcpy(ftn->string, packed_ftn + sizeof *ftn, ftn->blk_size);

    ftn->block = seeded_select_blocks(
            filesize_in_blocks, ftn->num_blocks, ftn->seed);

    if (!ftn->block) goto free_string;

    return ftn;
free_string:
    free(ftn->string);
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
        if (hold->fountain[i].block) free(hold->fountain[i].block);
    }
    if (hold->mark) free(hold->mark);
    if (hold->fountain) free(hold->fountain);
    free(hold);
}

/* Remove the ith item from the hold and return a copy of it */
fountain_s* packethold_remove(packethold_s* hold, int pos) {
    fountain_s* output = malloc(sizeof *output);
    if (!output) return NULL;
    *output = hold->fountain[pos];

    char* mark = hold->mark;
    for (int j = pos; j < hold->num_packets - 1; j++) {
        hold->fountain[j] = hold->fountain[j+1];
        if (ISBITSET(mark, j)) SETBIT(mark, j + 1);
        else CLEARBIT(mark, j + 1);
    }
    memset(hold->fountain + hold->offset - 1, 0, sizeof *hold->fountain);
    CLEARBIT(mark, hold->num_packets);
    hold->offset--;
    hold->num_packets--;

    /* Check that our packhold is not overly large */
    if (hold->num_slots > 2 * hold->offset && hold->num_slots > BUFFER_SIZE) {
        debug("reducing packethold size");
        odebug("%d", hold->num_packets);
        odebug("%zd", hold->offset);
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

    if (fountain_copy(&hold->fountain[hold->offset++], ftn) < 0)
        return ERR_MEM;
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
        for (int j = 0; j < ftn->num_blocks; j++)
            fprintf(stderr, " %d", ftn->block[j]);
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

