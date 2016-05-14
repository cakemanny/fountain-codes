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
// The file information that is sent - TODO: switch to uniform size members
// e.g. uint8_t, ..
typedef struct file_info_s {
    int32_t magic;          // Should always be MAGIC_INFO
    int16_t blk_size;
    int16_t num_blocks;
    int32_t filesize;       // The actual size in bytes
    char filename[256];
} file_info_s;

//
// This is sent by the client when it would like to receive a burst
// transmission from the server
#define MAGIC_WAITING ('W'<<24 | 'A'<<16 | 'I'<<8 | 'T')

typedef struct wait_signal_s {
    int32_t magic;
    int32_t capacity; // This could probably be an int16_t ...
} wait_signal_s;

#define fp_from(x)  x = _Generic((x), int16_t: ntohs(x), int32_t: ntohl(x))
#define fp_to(x)  x = _Generic((x), int16_t: htons(x), int32_t: htonl(x))

#endif /* __FOUNTAINPROTOCOL_H__ */
