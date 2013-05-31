#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "fountain.h"

#define BUFFER_SIZE 256

static inline void memerror(int line) {
    printf("Memory allocation error, Line: %d", line);
    return;
}

static char * xorncpy (char* destination, const char* source, register size_t n) {
    register char* d = destination;
    register const char* s = source;
    do {
        if (*s) *d++ ^= *s++;
        else break;
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
    if (!blocks) return -1;

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

    if (select_blocks(n, output) < 0) goto free_ob;

    // XOR blocks together
    output->string = calloc(blk_size + 1, sizeof *(output->string));
    if (!output->string) goto free_os;

    char * buffer = malloc(blk_size);
    if (!buffer) goto free_buffer;

    for (int i = 0; i < output->num_blocks; i++) {
        int m = output->block[i] * blk_size;
        fseek(f, m, SEEK_SET); /* m bytes from beginning of file */
        fread(buffer, 1, blk_size, f);
        xorncpy(output->string, buffer, blk_size);
    }

    // Cleanup
    free(buffer);

    return output;

    //free(buffer) // dead
free_buffer:
free_os:
free_ob:
    free_fountain(output);
    return NULL;
}

fountain_s* make_fountain(const char* string, int blk_size) {
    fountain_s* output = malloc(sizeof *output);
    if (output == NULL) return NULL;

    memset(output, 0, sizeof *output);
    int n = size_in_blocks(string, blk_size);

    if (select_blocks(n, output) < 0) goto free_ob;

    // XOR blocks together
    output->string = calloc(blk_size+1, sizeof *(output->string));
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
    if (( ret = (ftn1->num_blocks - ftn2->num_blocks) ))
        return ret;
    if (( ret = strcmp(ftn1->string, ftn2->string) ))
        return ret;

    for (int i=0; i < ftn1->num_blocks; ++i) {
        if (( ret = (ftn1->block[i] - ftn2->block[i]) ))
            return ret;
    }

    return 0;
}

packethold_s* packethold_new() {
    packethold_s tmp = {.num_slots=BUFFER_SIZE, .num_packets=0, .offset=0};
    packethold_s* hold = malloc(sizeof *hold);
    if (!hold) return NULL;

    *hold = tmp;

    hold->fountain = malloc(BUFFER_SIZE * sizeof *(hold->fountain));
    if (!hold->fountain) goto free_hold;

    return hold;
free_hold:
    packethold_free(hold);
    return NULL;
}

void packethold_free(packethold_s* hold) {
    if (hold->fountain) free(hold->fountain);
    free(hold);
}

//TODO: Create a stateless decode_fountain and store state in some structures

/* State struct could look like
 *
 * int* blkdecoded;
 * packethold_s* hold;
 * int packets_so_far
 */

char* decode_fountain(const char* string, int blk_size) {
    int n = size_in_blocks(string, blk_size);
    char * output = calloc(strlen(string) + 1 , sizeof *output);
    if (output == NULL) {
        memerror(__LINE__);
        return NULL;
    }
    fountain_s* curr_fountain = NULL;
    packethold_s hold = {.num_slots=BUFFER_SIZE, .num_packets=0, .offset=0};

    // Create the hold for storing packets for later
    hold.fountain = malloc(BUFFER_SIZE * sizeof *(hold.fountain));
    if (hold.fountain == NULL) {
        memerror(__LINE__);
        goto hold_exit;
    }

    int * blkdecoded = calloc(n, sizeof *blkdecoded);
    if (blkdecoded == NULL) {
        memerror(__LINE__);
        goto exit;
    }

    int i, j, solved = 0, newfount = 1, f_num = 0;

    while (!solved) {
        // recv fountain packet
        if (newfount) {
            curr_fountain = make_fountain(string, blk_size);
            f_num++;
        }
        newfount = 1;

        // Case one, block size one
        if (curr_fountain->num_blocks == 1) {
            if (blkdecoded[curr_fountain->block[0]] == 0) {
                strncpy(output + (curr_fountain->block[0] * blk_size),
                        curr_fountain->string, blk_size);
                blkdecoded[curr_fountain->block[0]] = 1;
            } else {
                continue; // continue if receiving solved block
            }

            //Part two check against blocks in hold
            int match = 0;
            for (i = 0; i < hold.num_packets; i++) {
                for (j = 0; j < hold.fountain[i].num_blocks; j++ ) {
                    if (hold.fountain[i].block[j] == curr_fountain->block[0]) {
                        // Xor out the hold block
                        xorncpy(hold.fountain[i].string,
                                curr_fountain->string,
                                blk_size);

                        // Remove removed blk number
                        for (j = i; j < hold.fountain[i].num_blocks-1; j++) {
                            hold.fountain[i].block[j] =
                                hold.fountain[i].block[j + 1];
                        }
                        j = hold.fountain[i].num_blocks - 1;
                        hold.fountain[i].block[j] = 0;
                        // Reduce number of blocks held
                        hold.fountain[i].num_blocks--;
                        match = 1;
                        break;
                    }
                }
                if (!match) continue;
                // on success check if hold packet is of size one block
                if (hold.fountain[i].num_blocks == 1) {
                    //move into output
                    if (blkdecoded[hold.fountain[i].block[0]] == 0) {
                        strncpy(output + (hold.fountain[i].block[0]*blk_size),
                                hold.fountain[i].string,
                                blk_size);

                        blkdecoded[hold.fountain[i].block[0]] = 1;
                    }
                    //remove from hold
                    for (j = i; j < hold.num_packets - 1; j++) {
                        hold.fountain[j] = hold.fountain[j+1];
                    }
                    memset(hold.fountain + hold.offset - 1, 0,
                            sizeof *(hold.fountain));
                    hold.offset--;
                    hold.num_packets--;
                }
            }
        } else {
            //Check against solved blocks
            for (i = 0; i < curr_fountain->num_blocks; i++) {
                if (blkdecoded[curr_fountain->block[i]]) {
                    //Xor the decoded block out of new packet
                    xorncpy(curr_fountain->string,
                            output + (curr_fountain->block[i]*blk_size),
                            blk_size);

                    // Remove decoded block number
                    for (j = i; j < curr_fountain->num_blocks - 1; j++) {
                        curr_fountain->block[j] = curr_fountain->block[j+1];
                    }
                    j = curr_fountain->num_blocks - 1;
                    curr_fountain->block[j] = 0;

                    //reduce number of blocks held
                    curr_fountain->num_blocks--;

                    //retest current reduced packet
                    newfount = 0;
                    break;
                }
            }
            if (!newfount) continue;

            // check if packet is already in hold
            int inhold = 0;
            for (i = 0; i < hold.num_slots; i++) {
                if (!cmp_fountain(curr_fountain, hold.fountain + i)) {
                    inhold = 1;
                    break;
                }
            }
            if (!inhold) {
                // Add packet to hold
                if (hold.offset >= hold.num_slots) {
                    int multi = (hold.num_slots / BUFFER_SIZE) + 1;

                    hold.fountain = realloc(hold.fountain,
                            multi * BUFFER_SIZE * sizeof *(hold.fountain));

                    if (hold.fountain == NULL) {
                        memerror(__LINE__);
                        goto exit;
                    }
                    hold.num_slots = multi * BUFFER_SIZE;

                }
                hold.fountain[hold.offset++] = *curr_fountain;
                hold.num_packets++;
            }

            free(curr_fountain); /* we can free it now that it's been copied */
        }

        // update solved
        solved = 1;
        for (i = 0; i < n; i++) {
            if(!blkdecoded[i]) {
                solved = 0;
                break;
            }
        }
    }
    printf("Number of packets required: %d\n", f_num);
    // Cleanup
    free_fountain(curr_fountain);

    return (char*) output;

exit:
    if (blkdecoded) free(blkdecoded);
hold_exit:
    if (hold.fountain) free_fountain(hold.fountain);

    return NULL;
}

