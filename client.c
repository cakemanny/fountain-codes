#define _GNU_SOURCE // asks stdio.h to include asprintf
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#ifdef _WIN32
#   include "asprintf.h"
#endif
#include "errors.h"
#include "fountain.h"
#include "dbg.h"

#define DEFAULT_PORT 2534
#define DEFAULT_IP "127.0.0.1"
#define ENDL "\r\n"
#define BUF_LEN 512
#define BURST_SIZE 1000

// ------ types ------
typedef fountain_s* (*fountain_src)(void);

typedef struct server_s {
    struct sockaddr_in address;
} server_s;

// ------ static variables ------
static SOCKET s = INVALID_SOCKET;
static int port = DEFAULT_PORT;
static char* remote_addr = DEFAULT_IP;
static char const * program_name = NULL;

static server_s curr_server = {};

static char * outfilename = NULL;
static int blk_size = 128;

// ------ functions ------
static void print_usage_and_exit(int status) {
    printf("Usage: %s [OPTION]... FILE\n", program_name);

    exit(status);
}

int main(int argc, char** argv) {
    /* deal with options */
    program_name = argv[0];
    int c;
    while ( (c = getopt(argc, argv, "i:o:p:")) != -1 ) {
        switch (c) {
            case 'i':
                remote_addr = optarg;
                break;
            case 'o':
                outfilename = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "bad option %s\n", optopt);
                print_usage_and_exit(1);
                break;
        }
    }

    int error;
    if ( (error = create_connection()) < 0 ) {
        close_connection();
        return handle_error(ERR_CONNECTION);
    }

    server_s server = { .address={
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = inet_addr(remote_addr)
    } };
    curr_server = server;

    //TODO
    if (!outfilename) outfilename = get_remote_filename();

    // do { get some packets, try to decode } while ( not decoded )
    proc_file(from_network);

    close_connection();
}

int create_connection() {

    if (WSAStartup(0x0202, &w)) return -10;
    if (w.wVersion != 0x0202)   return -20;

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)    return -30;

    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    WSACleanup();
}

static int send_msg(server_s server, char const * msg) {
    sendto(s, msg, strlen(msg), 0,
            (struct sockaddr*)&server.address,
            sizeof server.address); //FIXME - check return code
    return 0;
}

// unpack the fountain... do we allocate new memory, yes because of free later
// with inner structs
static fountain_s* unpack_fountain(char const * packed_ftn) {
    if (!packed_ftn) return NULL;

    fountain_s* ftn = malloc(sizeof *ftn);
    if (!ftn) return handle_error(ERR_MEM, NULL);
    memcpy(ftn, packed_ftn, sizeof *ftn);

    ftn->string = malloc(ftn->blk_size);
    if (!ftn->string) goto free_fountain;
    memcpy(ftn->string, packed_ftn + sizeof *ftn, ftn->blk_size);

    ftn->block = malloc(ftn->num_blocks * sizeof *ftn->block);
    if (!ftn-block) goto free_string;
    memcpy(ftn->block, packed_ftn + sizeof *ftn + ftn->blk_size,
            ftn->num_blocks * sizeof *ftn->block);

    return ftn;
free_string:
    free(ftn->string);
free_fountain:
    free(ftn);
    // should we have a more resilient handler / a kinder one...
    return handle_error(ERR_MEM, NULL); 
}

static fountain_s* from_network() {
    send_msg(curr_server, "FCWAITING" ENDL);
}

static int nsize_in_blocks() {
    send_msg(curr_server, "SIZEINBLOCKS" ENDL);
    //TODO set up a buffer to recv the size in
    
    char buf[BUF_LEN];
    int bytes_recvd = 0;

    struct sockaddr_in remote_addr;
    int remote_addr_size = sizeof remote_addr;

    do {
        memset(buf, '\0', BUF_LEN);
        bytes_recvd = recvfrom(s, buf, BUF_LEN, 0,
                        (struct sockaddr*)&remote_addr,
                        &remote_addr_size);
        if (bytes_recvd < 0)
            return ERR_NETWORK;
    } while (remote_addr != curr_server.address);

    int output = 0; // Shouldn't the following be 1+ rather than 1 -
    if (memcmp(buf, "SIZEINBLOCKS ", 1 - sizeof "SIZEINBLOCKS ") == 0) {
        for (int i = 0; i < BUF_LEN - 1; i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n') {
                buf[i] = 0;
                buf[i+1] = 0;
            }
        }
        output = atoi(buf + sizeof "SIZEINBLOCKS ");
    }

    //CONTINUE HERE
}

/* process fountains as they come down the wire
   returns a status code (see errors.c)
 */
static int proc_file(fountain_src ftn_src) {
    int result = 0;
    char * err_str = NULL;

    // prepare to do some output
    int num_blocks = nsize_in_blocks();

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

