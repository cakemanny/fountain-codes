#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "asprintf.h"
#include "fountain.h"

static int filesize(char const * filename) {
    FILE* f = fopen(filename, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        int size = ftell(f);
        fclose(f);
        return size;
    }
    return ERR_FOPEN;
}

int proc_file(char const * filename) {
    int result = 0;
    char * err_str = NULL;
    // open the input file
    FILE* infile = fopen(filename, "rb");
    if (!infile) return ERR_FOPEN;

    
    printf("Opened the input file successfully: %s\n", filename); //DEBUG

    // prepare to do some output
    int blk_size = 128;
    int fsize = filesize(filename);
    int num_blocks = (fsize % blk_size)
        ? (fsize / blk_size) + 1 : fsize / blk_size;

    decodestate_s* state = decodestate_new(blk_size, num_blocks);
    if (!state) {result = ERR_MEM; goto free_inf; }

    char * outfilename = NULL;
    if (asprintf(&outfilename, "%s.output", filename) < 0) {
        result = ERR_MEM; goto close_infile; }

    state->filename = outfilename;
    state->fp = fopen(outfilename, "wb+");
    if (!state->fp) {
        result = ERR_FOPEN; err_str = outfilename; goto free_state; }

    fountain_s* ftn = NULL;
    do {
        ftn = fmake_fountain(infile, blk_size);
        state->packets_so_far++;
        result = fdecode_fountain(state, ftn);
        free(ftn);
        if (result < 0) goto cleanup;
    } while (!decodestate_is_decoded(state));

cleanup:
    fclose(state->fp);
free_state:
    decodestate_free(state);
free_inf:
    free(outfilename);
close_infile:
    fclose(infile);
    return handle_error(result, err_str);
}

/* Program entry point */
int main(int argc, char** argv) {
    char* filename = NULL;

    int c;
    while ( (c = getopt(argc, argv, "f:")) != -1) {
        switch (c) {
            case 'f':
                filename = optarg;
                break;
        }
    }

    /* seed random number generation */
    srand(time(NULL));

    if (filename) {
        int error;
        if ((error = proc_file(filename)) < 0) {
            handle_error(error, filename); return 1; }
        printf("Output written to %s.output", filename);
        return 0;
    }

    char * input;
    if (argc != 2)
        input = "Hello there you jammy little bugger!";
    else
        input = argv[1];

    int blk_size = 13;

    char * decoded = decode_fountain(input, blk_size);
    printf("Input-: %s\n", input);
    printf("Output: %s\n", decoded);
    free(decoded);

    return 0;
}

