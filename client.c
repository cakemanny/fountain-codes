#define _GNU_SOURCE // asks stdio.h to include asprintf

/* Load in OS specific networking code */
#include "networking.h"

#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#include <unistd.h> //getopt

#ifdef _WIN32
#   include "asprintf.h"
#endif
#include "errors.h"
#include "fountain.h"
#include "dbg.h"
#include "fountainprotocol.h" // msg definitions

#define DEFAULT_PORT 2534
#define DEFAULT_IP "127.0.0.1"
#define ENDL "\r\n"
#define BUF_LEN 512 // 4 times the blksize -- should be ok
#define BURST_SIZE 1000

// ------ types ------
typedef fountain_s* (*fountain_src)(void);

typedef struct server_s {
    struct sockaddr_in address;
} server_s;

typedef struct ftn_cache_s {
    int capacity;
    int size;
    fountain_s** base;
    fountain_s** current;
} ftn_cache_s;


// ------ Forward declarations ------
static int proc_file(fountain_src ftn_src);
static int create_connection();
static void close_connection();
static fountain_s* from_network();
static char* get_remote_filename();

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
    fputs("\
\n\
  -i        ip address of the remote host\n\
  -o        output file name\n\
  -p        port to connect to\n\
", stdout);
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
                fprintf(stderr, "bad option %c\n", optopt);
                print_usage_and_exit(1);
                break;
        }
    }

    int error;
    if ( (error = create_connection()) < 0 ) {
        close_connection();
        return handle_error(ERR_CONNECTION, NULL);
    }

    server_s server = { .address={
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = inet_addr(remote_addr)
    } };
    curr_server = server;

    int i_should_free_outfilename = 0;
    if (!outfilename) {
        outfilename = get_remote_filename();
        i_should_free_outfilename = 1;
    }

    // do { get some packets, try to decode } while ( not decoded )
    proc_file(from_network);

    if (i_should_free_outfilename)
        free(outfilename);
    close_connection();
}

int create_connection() {

    #ifdef _WIN32
    WSADATA w;
    if (WSAStartup(0x0202, &w)) return -10;
    if (w.wVersion != 0x0202)   return -20;
    #endif

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)    return -30;

    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    #ifdef _WIN32
    WSACleanup();
    #endif
}

static int send_msg(server_s server, char const * msg) {
    debug("About to send msg: %s", msg);
    sendto(s, msg, strlen(msg), 0,
            (struct sockaddr*)&server.address,
            sizeof server.address); //FIXME - check return code
    return 0;
}

static int recv_msg(char* buf, size_t buf_len) {
    int bytes_recvd = 0;
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_size = sizeof remote_addr;

    do {
        debug("Clearing buffer and waiting for reponse from %s",
                inet_ntoa(curr_server.address.sin_addr));
        memset(buf, '\0', BUF_LEN);
        bytes_recvd = recvfrom(s, buf, BUF_LEN, 0,
                        (struct sockaddr*)&remote_addr,
                        &remote_addr_size);
        if (bytes_recvd < 0)
            return ERR_NETWORK;

        debug("Received %d bytes from %s",
                bytes_recvd, inet_ntoa(remote_addr.sin_addr));

        /* Make sure that this is from the server we made the request to,
           otherwise ignore and try again */
    } while (memcmp((void*)&remote_addr.sin_addr,
                (void*)&curr_server.address.sin_addr,
                sizeof remote_addr.sin_addr) != 0);

    return bytes_recvd;
}

char* get_remote_filename() {
    send_msg(curr_server, MSG_FILENAME ENDL);
    char buf[BUF_LEN];
    int bytes_recvd = recv_msg(buf, BUF_LEN);
    if (bytes_recvd < 0) return NULL;
    if (memcmp(buf, HDR_FILENAME, sizeof HDR_FILENAME -1) == 0) {
        char * tmp;
        if (asprintf(&tmp, "%s", buf + sizeof HDR_FILENAME) < 0)
            return NULL;
        return tmp; // this will get free'd in main
    }
    // Should really consider doing a few retries at this point
    return NULL;
}

static void ftn_cache_alloc(ftn_cache_s* cache) {
    cache->base = malloc(BURST_SIZE * sizeof *cache->base);
    if (cache->base == NULL) return;

    cache->capacity = BURST_SIZE;
    cache->current = cache->base;
}

static void load_from_network(ftn_cache_s* cache) {
    send_msg(curr_server, MSG_WAITING ENDL);
    
    char buf[BUF_LEN];


    // we need to do these either 750 times or... have a timeout
    // we might also want to adjust our yield expectation depending
    // on whether we are hitting those timeouts or not...
    for (int i = 0; i < 200; i++) {
        int bytes_recvd = recv_msg(buf, BUF_LEN);
        if (bytes_recvd < 0)
            handle_error(bytes_recvd, NULL);

        cache->base[i] = unpack_fountain(buf);
        cache->size++;
        debug("Cache size is now %d", cache->size);
    }
    cache->current = cache->base;
}

fountain_s* from_network() {
    static ftn_cache_s cache = {};
    if (cache.base == NULL) {
        ftn_cache_alloc(&cache);
        if (cache.base == NULL) return NULL;
    }

    if (cache.size == 0) {
        debug("Cache size 0 - loading from network...");
        load_from_network(&cache);
        if (cache.size == 0) return NULL;
    }

    fountain_s* output = *cache.current;
    *cache.current++ = NULL;
    --cache.size;
    return output;
}

static int nsize_in_blocks() {
    send_msg(curr_server, MSG_SIZE ENDL);
    
    char buf[BUF_LEN];
    int bytes_recvd = recv_msg(buf, BUF_LEN);
    if (bytes_recvd < 0) return ERR_NETWORK;

    int output = 0;
    if (memcmp(buf, HDR_SIZE, sizeof HDR_SIZE - 1) == 0) {
        debug("Message received: %s", buf);
        output = atoi(buf + sizeof HDR_SIZE - 1);
        debug("Filesize in blocks: %d", output);
        return output;
    }
    // Really we ought to try again until we get te msg we want
    return -1; // given it an error code I guess...guess
    //CONTINUE HERE
}

/* process fountains as they come down the wire
   returns a status code (see errors.c)
 */
int proc_file(fountain_src ftn_src) {
    int result = 0;
    char * err_str = NULL;

    // prepare to do some output
    int num_blocks = nsize_in_blocks();
    if (num_blocks < 0)
        return handle_error(num_blocks, err_str);

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

