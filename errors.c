#include "errors.h"
#include <stdio.h> //fprintf
#include <stdlib.h> //exit

#ifdef _WIN32
#   define ENDL "\r\n"
#else
#   define ENDL "\n"
#endif
#define ERR stderr

#define pe(M, ...) fprintf(ERR, "[ERROR]: " M, ##__VA_ARGS__)
#define fargs(A) ((A) ? *((char const **)(A)) : "_output_file_")
#define margs(A) ((A) ? *((char const **)(A)) : "")

int handle_error(int error_number, void* args) {
    switch (error_number) {
        case ERR_FOPEN:
            pe("Couldn't open file %s" ENDL, fargs(args));
            exit(error_number);
            break;
        case ERR_MEM:
            pe("Couldn't allocate memory %s" ENDL, margs(args));
            exit(error_number);
            break;
        case ERR_BWRITE:
            pe("Writing to the output file: %s" ENDL, fargs(args));
            break;
        case ERR_BREAD:
            pe("Unable to read from the output file: %s" ENDL, fargs(args));
            break;
        case ERR_PACKET_ADD:
            pe("An error occured when trying to reallocate memory for the "
                    "hold %s" ENDL, margs(args));
            exit(error_number);
            break;
        case ERR_PACKING:
            pe("An error occured while trying to pack the packet" ENDL);
            break;
        default:
            return 0;
    }
    return error_number;
}

