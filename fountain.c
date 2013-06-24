#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "errors.h"
#include "fountain.h"
#include "dbg.h"

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

static int choose_num_blocks(const int n) {
    const int d_size = n*(n+1)/2;
    int* dist = malloc(d_size * sizeof *dist);
    if (!dist) return 1; /* We ran out of memory, should probs die, but
                          * maybe we can squeeze out one more block */

    int* lpdist = dist;
    for (int m = n; m > 0; m--) {
        for (int i = 0; i < m; i++) {
            *lpdist++ = n - m + 1;
        }
    }

    int d = dist[rand() % d_size];

    free(dist);
    return d;
}

static int select_blocks(const int n, fountain_s* ftn) {
    int d = choose_num_blocks(n);

    int* blocks = malloc(d * sizeof *blocks);
    if (!blocks) return ERR_MEM;

    for (int i = 0; i < d; i++) {
        blocks[i] = rand() % n;
        for (int j = 0; j < i; j++) {
            if (blocks[i] == blocks[j]) {
                i--;
                break;
            }
        }
    }

    ftn->num_blocks = d;
    ftn->block = blocks;
    return 0;
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

    if (select_blocks(n, output) < 0) goto free_ftn;

    // XOR blocks together
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

    if (select_blocks(n, output) < 0) goto free_ob;

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

int fountain_copy(fountain_s* dst, fountain_s* src, int blk_size) {
    dst->num_blocks = src->num_blocks;

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
            for(int j = 0; j < hold->fountain[i].num_blocks; j++) {
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
    } else { /* size > 1, check against unsolved blocks */
        for (int i = 0; i < ftn->num_blocks; i++) {
            if (blkdec[ftn->block[i]]) {
                // Xor the decoded block out of a new packet
                char buf[blk_size];
                memset(buf, 0, blk_size);
                bread(buf, ftn->block[i], state);
                xorncpy(ftn->string, buf, blk_size);

                // Remove the decoded block number
                for (int j = i; j < ftn->num_blocks - 1; j++) {
                    ftn->block[j] = ftn->block[j+1];
                }
                ftn->block[ftn->num_blocks - 1] = 0; 

                // reduce number of blocks held
                ftn->num_blocks--;

                // retest current reduced packet
                retest = true;
                break;
            }
        }
        if (retest) return _decode_fountain(state, ftn, bread, bwrite);

        bool inhold = false;
        for (size_t i = 0; i <= hold->offset; i++) {
            if (cmp_fountain(ftn, hold->fountain + i) == 0) {
                inhold = true;
                break;
            }
        }
        if (!inhold) { /* Add packet to hold */
            if (packethold_add(hold, ftn, blk_size) < 0)
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
    char* output = calloc(size_in_blocks(string, blk_size), blk_size);
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

/* ============ Packhold Functions ========================================= */

packethold_s* packethold_new() {
    packethold_s* hold = malloc(sizeof *hold);
    if (!hold) return NULL;

    packethold_s tmp = {.num_slots=BUFFER_SIZE, .num_packets=0, .offset=0};
    *hold = tmp;

    hold->fountain = calloc(BUFFER_SIZE, sizeof *hold->fountain);
    if (!hold->fountain) goto free_hold;

    return hold;
free_hold:
    packethold_free(hold);
    return NULL;
}

void packethold_free(packethold_s* hold) {
    for (int i = 0; i < hold->num_packets; i++) {
        if (hold->fountain[i].string) free(hold->fountain[i].string);
        if (hold->fountain[i].block) free(hold->fountain[i].block);
    }
    if (hold->fountain) free(hold->fountain);
    free(hold);
}

/* Remove the ith item from the hold and return a copy of it */
fountain_s* packethold_remove(packethold_s* hold, int pos) {
    fountain_s* output = malloc(sizeof *output);
    if (!output) return NULL;
    *output = hold->fountain[pos];

    for (int j = pos; j < hold->num_packets - 1; j++)
        hold->fountain[j] = hold->fountain[j+1];
    memset(hold->fountain + hold->offset - 1, 0, sizeof *hold->fountain);
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
        hold->num_slots = hold->num_packets;
    }

    return output;
}

int packethold_add(packethold_s* hold, fountain_s* ftn, int blk_size) {
    if (hold->offset >= hold->num_slots) {
        int space = 3 * hold->num_slots /2;
        odebug("%d", space);

        fountain_s* tmp_ptr = hold->fountain;
        hold->fountain = realloc(hold->fountain, space * sizeof *hold->fountain);
        if (hold->fountain == NULL) {
            hold->fountain = tmp_ptr;
            return REALLOC_ERR;
        }
        
        hold->num_slots = space;
    }
    
    if (fountain_copy(&hold->fountain[hold->offset++], ftn, blk_size) < 0)
        return ERR_MEM;
    hold->num_packets++;
    return 0;
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

    decodestate_s tmp = {.num_blocks = num_blocks, .packets_so_far = 0,
                         .blk_size = blk_size};
    *output = tmp;

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

