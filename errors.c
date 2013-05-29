#include "errors.h"
#include <stdio.h> //fprintf
#include <stdlib.h> //exit

#define ENDL "\r\n"
#define ERR stderr

#define pe(...) fprintf(ERR, __VA_ARGS__)

void handle_error(int error_number, void* args) {
    switch (error_number) {
        case ERR_FOPEN:
            pe("Error: Couldn't open file %s" ENDL,
                    args ? *((char const **)args) : "");
            exit(error_number);
            break;
        case ERR_MEM:
            pe("Error: couldn't allocate memory");
        default:
            return;
    }
}

