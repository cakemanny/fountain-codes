#ifndef __FOUNTAIN_H__
#define __FOUNTAIN_H__

/* Structure definitions */
typedef struct fountain {
    char* string;
    int num_blocks;
    // they start from block 0
    int* block;
} fountain_t;

typedef struct packethold {
    int iPackets;
    int iSlots;
    fountain_t * fountain;
    size_t offset;
} packethold_t;

/* Function declarations */
int size_in_blocks(const char* string); /* we should be getting rid of this */
fountain_t* make_fountain(const char* string); /* allocates memory */
void free_fountain(fountain_t* ftn);
int cmp_fountain(fountain_t* ftn1, fountain_t* ftn2);
char* decode_fountain(const char* string, int n /*number of blocks*/);

#endif /* __FOUNTAIN_H__ */
