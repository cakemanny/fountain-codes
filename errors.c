#include "errors.h"
#include <stdio.h> //fprintf
#include <stdlib.h> //exit

#define ENDL "\r\n"
#define ERR stderr

#define pe(...) fprintf(ERR, __VA_ARGS__)
#define fargs(x) ((x) ? *((char const **)(x)) : "_output_file_")
#define margs(x) ((x) ? *((char const **)(x)) : "")
int handle_error(int error_number, void* args) {
    switch (error_number) {
        case ERR_FOPEN:
            pe("Error: Couldn't open file %s" ENDL, fargs(args));
            exit(error_number);
            break;
        case ERR_MEM:
            pe("Error: couldn't allocate memory %s" ENDL, fargs(args));
            break;
        case ERR_BWRITE:
            pe("Error writing to the output file: %s" ENDL, fargs(args));
            break;
        case ERR_BREAD:
            pe("Error unable to read from the output file: %s" ENDL, fargs(args));
            break;
        case ERR_PACKET_ADD:
            pe("An error occured when trying to reallocate memory for the hold" ENDL);
            break;
        default:
            return 0;
    }
    return error_number;
}

