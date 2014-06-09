#ifndef __FOUNTAINPROTOCOL_H__
#define __FOUNTAINPROTOCOL_H__
/*
  fountainprotocol.h

  This is where we define the message syntax for protocol.
*/


//
// This is sent by the client when it would like to receive a burst
// transmission from the server
#define MSG_WAITING "FCWAITING"

//
// Request for size of the file in the block size being used
#define MSG_SIZE    "NUMBLOCKS"
#define HDR_SIZE    "NUMBLOCKS "

//
// Request for the block size
#define MSG_BLKSIZE "BLOCKSIZE"
#define HDR_BLKSIZE "BLOCKSIZE "

//
// Request for the filename
#define MSG_FILENAME "FILENAME"
#define HDR_FILENAME "FILENAME "

//
// A request for info about the file being served
#define MSG_INFO    "FileInfo"
#define MAGIC_INFO  54119

#define MSG_FILESIZE    "FileSize"

//
// The file information that is sent
typedef struct file_info_s {
    int magic;          // Should always be MAGIC_INFO
    int blk_size;
    int num_blocks;
    int filesize;       // The actual size in bytes
    char filename[256];
} file_info_s;

#endif /* __FOUNTAINPROTOCOL_H__ */
