#ifndef __FOUNTAIN_H__
#define __FOUNTAIN_H__

#include <stdio.h> // FILE
#include <stdint.h>
#include "errors.h"
#include "platform.h"

#define MAX_BLOCK_SIZE 16384

/* ------ Structure definitions ------ */

typedef struct fountain_s {
    int32_t num_blocks; // This doesn't really need to be so large
    int16_t blk_size;
    uint16_t section;
    uint64_t seed;
    char* string;   // TODO: rename this "data"
    uint64_t block_set_len;
#ifdef __x86_64__
    uint64_t* block_set;
#else
    uint32_t* block_set; // Use bitset on receiving end
#endif
} fountain_s;

/* We don't want to send the pointers across the network as they will have
 * different sizes on different systems
 */
#define FTN_HEADER_SIZE (sizeof(int32_t) + sizeof(int16_t) + sizeof(uint16_t) + sizeof(uint64_t))

/* include the checksum at the beginning */
#define MAX_PACKED_FTN_SIZE (sizeof(int16_t) + FTN_HEADER_SIZE + MAX_BLOCK_SIZE)

typedef struct packethold_s {
    int num_packets;
    int num_slots;
    fountain_s * fountain; /**< an array of held packets */
    size_t offset;
    char* mark; /* bitset for mark algorithm */
    char* deleted; /* bitset for marking packets as deleted */
#ifdef __x86_64__
    uint64_t* block_sets;
#else
    uint32_t* block_sets; // Use bitset on receiving end
#endif
} packethold_s;

/** This is the structure we keep the state of our decoding in. */
typedef struct decodestate_s {
    int blk_size;
    int num_blocks;
    char* blkdecoded;   // TODO: probably ought to change to bitset
    packethold_s* hold;
    int packets_so_far;
    char* filename; /* must be in wb+ mode */
    FILE* fp;
} decodestate_s;

typedef struct memdecodestate_s {
    union { // TODO Stop using anon union/struct to silence clang warning
        struct decodestate_s;
        decodestate_s state;
    };
    char * result;
} memdecodestate_s;

extern char* memdecodestate_filename;

/* Function declarations */

/* ============ fountain_s functions  ====================================== */
/**
 * \param string The block of memory to make the fountain from
 * \param blk_size The block size of the fountain
 * \param length The length of the block of memory
 * \returns pointer to a new fountain_s
 */
fountain_s* make_fountain(const char* string, int blk_size, size_t length, int section, int section_size) __malloc; /* allocs memory */
fountain_s* fmake_fountain(FILE* f, int blk_size, int section, int section_size) __malloc; /* allocs memory */
void free_fountain(fountain_s* ftn);
int cmp_fountain(fountain_s* ftn1, fountain_s* ftn2);
char* decode_fountain(const char* string, int blk_size);
void print_fountain(const fountain_s * ftn);

/**
 * Trys to decode the given fountain, it will write output to the file.
 * \returns 0 on success
 *          ALLOC_ERR if a memory allocation error occurs
 *          F_ALREADY_DECODED so, just for our records, we already have decoded
 *          this block.
 */
#ifndef F_ALREADY_DECODED
#   define F_ALREADY_DECODED 1
#endif
int fdecode_fountain(decodestate_s* state, fountain_s* ftn);

/* same as fdecode_fountain but more memdecodestate_s's */
int memdecode_fountain(memdecodestate_s* state, fountain_s* ftn);

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
fountain_s* unpack_fountain(buffer_s packet, int section_size_in_blocks) __malloc;

/* ============ packethold_s functions  ==================================== */
// num_blocks in the number in the result - not the length of the hold
packethold_s* packethold_new(int num_blocks) __malloc; /* allocs memory */
void packethold_free(packethold_s* hold);
fountain_s* packethold_remove(packethold_s* hold, int pos, fountain_s* output); /* allocs memory*/

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

void packethold_print(packethold_s* hold);

void packethold_collect_garbage(packethold_s* hold);

/* ============ decodestate_s functions ==================================== */

/* Creates a new packehold with items
     blk_size, num_blocks, blkdecoded, hold, packets_so_far
   assigned and initialized
     filename, fp
   initialized as NULL
 */
decodestate_s* decodestate_new(int blk_size, int num_blocks) __malloc;
void decodestate_free(decodestate_s* state);
int decodestate_is_decoded(decodestate_s* state);

#endif /* __FOUNTAIN_H__ */
