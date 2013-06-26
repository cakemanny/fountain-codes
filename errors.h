#ifndef __ERRORS_H__
#define __ERRORS_H__

#define ERR_NO_ERROR    ( 0)
#define ERR_FOPEN       (-1)
#define ERR_MEM         (-2)
#define ERR_BWRITE      (-3)
#define ERR_BREAD       (-4)
#define ERR_PACKET_ADD  (-5)
#define ERR_PACKING     (-6)

//#define VA_NUM_ARGS(...) VA_NUM_ARGS_IMPL(__VA_ARGS__, 5, 4, 3, 2, 1)
//#define VA_NUM_ARGS_IMPL(_1,_2,_3,_4,_5,N,...) N

int handle_error(int error_number, void* args);

#endif // __ERRORS_H__
