#ifndef __FOUNTAIN_H__
#define __FOUNTAIN_H__

#include <stdio.h> // FILE
#include "errors.h"

/* Structure definitions */

typedef struct fountain_s {
    char* string;
    int num_blocks;
    int* block; // they start from block 0 -- TODO rename this blocks
    int blk_size;
    // To add: unsigned long seed; but need to look at all the serialization
    // etc first
} fountain_s;

typedef struct packethold_s {
    int num_packets;
    int num_slots;
    fountain_s * fountain; /* an array of held packets */
    size_t offset;
} packethold_s;

/* This is the strcuture we keep the state of our decoding in */
typedef struct decodestate_s {
    int blk_size;
    int num_blocks;
    char* blkdecoded;
    packethold_s* hold;
    int packets_so_far;
    char* filename; /* must be in w+ mode */
    FILE* fp;
} decodestate_s;

typedef struct memdecodestate_s {
    union {
        struct decodestate_s;
        decodestate_s state;
    };
    char * result;
} memdecodestate_s;

/* Function declarations */

/* ============ fountain_s functions  ====================================== */
fountain_s* make_fountain(const char* string, int blk_size); /* allocs memory */
fountain_s* fmake_fountain(FILE* f, int blk_size); /* allocs memory */
void free_fountain(fountain_s* ftn);
int cmp_fountain(fountain_s* ftn1, fountain_s* ftn2);
char* decode_fountain(const char* string, int blk_size);

/* trys to decode the given fountain, it will write output to the file.
   returns 0 on success
   returns ALLOC_ERR if a memory allocation error occurs
   returns F_ALREADY_DECODED so, just for our records, we already have decoded
           this block.
 */
#ifndef F_ALREADY_DECODED
#   define F_ALREADY_DECODED 1
#endif
int fdecode_fountain(decodestate_s* state, fountain_s* ftn);

/* returns 0 on success
   returns ERR_MEM if mem allocation occurs for creating the pointed to
           elements.
 */
int fountain_copy(fountain_s* dst, fountain_s* src); /* allocs memory */

typedef struct buffer_s {
    int length;
    char* buffer;
} buffer_s;

/* Pack a fountain into a single buffer so that it can be sent across the
   network. Works by placing each part one after the other in memory

   returns A pointer to the buffer
*/
buffer_s pack_fountain(fountain_s* ftn);

/* Upack the fountain from it's serialized form.
   This does allocate memory because free_fountain will expect the inner
   structures to be freeable

   returns A pointer to the deserialized fountain or NULL on failure
*/
fountain_s* unpack_fountain(buffer_s packet);

/* ============ packethold_s functions  ==================================== */
packethold_s* packethold_new(); /* allocs memory */
void packethold_free(packethold_s* hold);
fountain_s* packethold_remove(packethold_s* hold, int pos); /* allocs memory*/

/* adds a fountain to the end of the hold, making full copy so the orig can
   be freed later if so desired.
   returns 0 on success
   returns REALLOC_ERR if unable to reallocate more memory for the hold
   returns ALLOC_ERR if unable to allocate to make a copy of the fountain
 */
#ifndef REALLOC_ERR
#   define REALLOC_ERR -2
#endif
int packethold_add(packethold_s* hold, fountain_s* ftn);


/* ============ decodestate_s functions ==================================== */

/* Creates a new packehold with items
     blk_size, num_blocks, blkdecoded, hold, packets_so_far
   assigned and initialized
     filename, fp
   initialized as NULL
 */
decodestate_s* decodestate_new(int blk_size, int num_blocks);
void decodestate_free(decodestate_s* state);
int decodestate_is_decoded(decodestate_s* state); 

#endif /* __FOUNTAIN_H__ */
