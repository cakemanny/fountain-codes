#ifndef __FOUNTAIN_H__
#define __FOUNTAIN_H__

#include <stdio.h> // FILE

/* Structure definitions */
typedef struct fountain_s {
    char* string;
    int num_blocks;
    // they start from block 0
    int* block;
} fountain_s;

typedef struct packethold_s {
    int num_packets;
    int num_slots;
    fountain_s * fountain;
    size_t offset;
} packethold_s;

/* Function declarations */
fountain_s* make_fountain(const char* string, int blk_size); /* allocates memory */
fountain_s* fmake_fountain(FILE* f, int blk_size); /* allocates memory */
void free_fountain(fountain_s* ftn);
int cmp_fountain(fountain_s* ftn1, fountain_s* ftn2);
char* decode_fountain(const char* string, int blk_size);

packethold_s* packethold_new();
void packethold_free(packethold_s* hold);

#endif /* __FOUNTAIN_H__ */
