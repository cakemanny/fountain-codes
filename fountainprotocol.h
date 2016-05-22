#ifndef __FOUNTAINPROTOCOL_H__
#define __FOUNTAINPROTOCOL_H__
/*
  fountainprotocol.h

  This is where we define the message syntax for protocol.
*/
#include <stdint.h>

typedef struct packet_s {
    int32_t magic;
    // More stuff below
    //
} packet_s;

//
// A request for info about the file being served
#define MAGIC_REQUEST_INFO  ('R'<<24 | 'I'<<16 | 'N'<<8 | 'F')
typedef struct info_request_s {
    int32_t magic;
    // Leave room to expand later
} info_request_s;

#define MAGIC_INFO  ('I'<<24 | 'N'<<16 | 'F'<<8 | 'O')
//
// The file information that is sent
typedef struct file_info_s {
    int32_t magic;          // Should always be MAGIC_INFO
    int16_t section_size;   // number of blocks per section
    int16_t blk_size;
    int32_t filesize;       // The actual size in bytes
    char filename[256];
} file_info_s;

//
// This is sent by the client when it would like to receive a burst
// transmission from the server
#define MAGIC_WAITING ('W'<<24 | 'A'<<16 | 'I'<<8 | 'T')

typedef struct wait_signal_s {
    int32_t magic;
    int16_t capacity; // This could probably be an int16_t ...
    uint16_t section;
} wait_signal_s;

#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
#endif

// Test for GCC 4.8.*
#if GCC_VERSION >= 40800

#define fp_from(x)  x = _Generic((x)\
        , int16_t: ntohs(x)\
        , uint16_t: ntohs(x)\
        , int32_t: ntohl(x)\
        , uint32_t: ntohl(x)\
        )
#define fp_to(x)  x = _Generic((x)\
        , int16_t: htons(x)\
        , uint16_t: htons(x)\
        , int32_t: htonl(x)\
        , uint32_t: htonl(x)\
        )
#else
//#define typeof(x)       __typeof__(x)
#define TEQ(t1, t2)     __builtin_types_compatible_p(t1, t2)
#define cexpr(C,E1,E2)  __builtin_choose_expr(C,E1,E2)
#define fp_from(x)   x = cexpr(sizeof(x)==2, ntohs(x), cexpr(sizeof(x)==4, ntohl(x), (void)0))
#define fp_to(x)     x = cexpr(sizeof(x)==2, htons(x), cexpr(sizeof(x)==4, htonl(x), (void)0))
#endif // GCC_VERSION

#endif /* __FOUNTAINPROTOCOL_H__ */
