#ifndef __FOUNTAIN_H__
#define __FOUNTAIN_H__

#include <stdio.h> // FILE

/* Structure definitions */
typedef struct fountain_s {
    char* string;
    int num_blocks;
    int* block; // they start from block 0
} fountain_s;

typedef struct packethold_s {
    int num_packets;
    int num_slots;
    fountain_s * fountain;
    size_t offset;
} packethold_s;

/* This is the strcuture we keep the state of our decoding in */
typedef struct decodestate_s {
    int blk_size;
    int num_blocks;
    char* blkdecoded;
    packethold_s* hold;
    int packets_so_far;
    char* filename;
    FILE* fp;
} decodestate_s;

/* Function declarations */

/* fountain_s functions */
fountain_s* make_fountain(const char* string, int blk_size); /* allocates memory */
fountain_s* fmake_fountain(FILE* f, int blk_size); /* allocates memory */
void free_fountain(fountain_s* ftn);
int cmp_fountain(fountain_s* ftn1, fountain_s* ftn2);
char* decode_fountain(const char* string, int blk_size);
int fdecode_fountain(decodestate_s* state, fountain_s* fountain);

/* packethold_s functions */
packethold_s* packethold_new(); /* allocs memory */
void packethold_free(packethold_s* hold);

/* decodestate_s functions */
decodestate_s* decodestate_new(int blk_size, int num_blocks);
void decodestate_free(decodestate_s* state);

#endif /* __FOUNTAIN_H__ */
