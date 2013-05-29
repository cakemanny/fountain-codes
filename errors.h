#ifndef __ERRORS_H__
#define __ERRORS_H__

#define ERR_FOPEN (-1)
#define ERR_MEM (-2)

void handle_error(int error_number, void* args);

#endif // __ERRORS_H__
