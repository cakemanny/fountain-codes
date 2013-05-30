#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fountain.h"

/* Program entry point */
int main(int argc, char** argv) {
/*
    int* dist = create_ddist(10);
    for (int* it = dist; *it >= 0; ++it)
        printf("%d", *it);
    printf("\n");
    free(dist);
*/
    srand(time(NULL));
    char * input;
    if (argc != 2)
        input = "Hello there you jammy little bugger!";
    else
        input = argv[1];

    int blk_size = 13;
    /*
    int i=0;
    for (;i < 10; ++i) {
        fountain_t* ftn = make_fountain(input, blk_size)
    }
    */

    char * decoded = decode_fountain(input, blk_size);
    printf("The decoded version of: %s\nIs: %s\n",input,decoded);

    return 0;
}

