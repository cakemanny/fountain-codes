#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h> //strlen
#include <time.h>
#include <unistd.h> //getopt
#ifdef _WIN_32
#   include "asprintf.h"
#endif
#include "fountain.h"
#include "dbg.h"

#ifdef _WIN_32
#   define ENDL "\r\n"
#else
#   define ENDL "\n"
#endif

// ----- types ------
typedef fountain_s* (*fountain_src)(void);

// ------ static variables ------
static char* infilename = NULL;
static char* outfilename = NULL;
static int blk_size = 128;
static char* meminput = "Hello there you jammy little bugger!";

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

static int fsize_in_blocks(char const * filename) {
    int fsize = filesize(filename);
    return (fsize % blk_size)
        ? (fsize / blk_size) + 1 : fsize / blk_size;
}

static int size_in_blocks(const char* string, int blk_size) {
    int string_len = strlen(string);
    return (string_len % blk_size)
        ? (string_len / blk_size) + 1 : string_len / blk_size;
}

static fountain_s* from_file() {
    fountain_s* output = NULL;
    if (infilename) {
        FILE* infile = fopen(infilename, "rb");
        if (infile) {
            output = fmake_fountain(infile, blk_size);
            fclose(infile);
        }
    }
    return output;
}

static fountain_s* from_mem() {
    return make_fountain(meminput, blk_size);
}

static int proc_file(fountain_src ftn_src) {
    int result = 0;
    char * err_str = NULL;

    // prepare to do some output
    int num_blocks = (ftn_src == from_file) ?
        fsize_in_blocks(infilename) : size_in_blocks(meminput, blk_size);

    decodestate_s* state = decodestate_new(blk_size, num_blocks);
    if (!state) return handle_error(ERR_MEM, NULL);

    state->filename = outfilename;
    state->fp = fopen(outfilename, "wb+");
    if (!state->fp) {
        result = ERR_FOPEN; err_str = outfilename; goto free_state; }

    fountain_s* ftn = NULL;
    do {
        ftn = ftn_src();
        if (!ftn) goto cleanup;
        state->packets_so_far++;
        result = fdecode_fountain(state, ftn);
        free_fountain(ftn);
        if (result < 0) goto cleanup;
    } while (!decodestate_is_decoded(state));

    odebug("%d", state->packets_so_far);

cleanup:
    fclose(state->fp);
free_state:
    decodestate_free(state);
    return handle_error(result, err_str);
}

/* Program entry point */
int main(int argc, char** argv) {
    int c;
    while ( (c = getopt(argc, argv, "f:o:")) != -1) {
        switch (c) {
            case 'f':
                infilename = optarg;
                break;
            case 'o':
                outfilename = optarg;
                break;
        }
    }
    if (optind < argc)
        meminput = argv[optind];

    /* seed random number generation */
    srand(time(NULL));

    fountain_src ftn_src = NULL;
    if (infilename)
        ftn_src = from_file;
    else
        ftn_src = from_mem;

    if (outfilename) {
        int error;
        if ((error = proc_file(ftn_src)) < 0) {
            handle_error(error, infilename); return 1; }
        printf("Output written to %s" ENDL, outfilename);
        return 0;
    }

    //int blk_size = 13;

    char * decoded = decode_fountain(meminput, blk_size);
    printf("Input-: %s" ENDL, meminput);
    printf("Output: %s" ENDL, decoded);
    free(decoded);

    return 0;
}

