#define _GNU_SOURCE // asks stdio.h to include asprintf

/* Load in OS specific networking code */
#include "networking.h"

#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#include <unistd.h> //getopt
#include <getopt.h> //getopt_long

#ifdef _WIN32
#   include <fcntl.h> // open -- the mingw unix open
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
static int proc_file(fountain_src ftn_src, file_info_s* file_info);
static int create_connection();
static void close_connection();
static fountain_s* from_network();
static int get_remote_file_info(struct file_info_s*);

// TODO: test use of long options on windows
struct option long_options[] = {
    { "help",   no_argument,        NULL, 'h' },
    { "ip",     required_argument,  NULL, 'i' },
    { "output", required_argument,  NULL, 'o' },
    { "port",   required_argument,  NULL, 'p' },
    { 0, 0, 0, 0 }
};

// ------ static variables ------
static SOCKET s = INVALID_SOCKET;
static int port = DEFAULT_PORT;
static char* remote_addr = DEFAULT_IP;
static char const * program_name = NULL;

static server_s curr_server = {};

static char * outfilename = NULL;

// The buffer for our pulling packets off the network
static char* netbuf;
static int netbuf_len;

static int filesize_in_blocks = 0;

// ------ functions ------
static void print_usage_and_exit(int status) {
    FILE* out = (status == 0) ? stdout : stderr;

    fprintf(out, "Usage: %s [OPTION]... FILE\n", program_name);
    fputs("\
\n\
  -h, --help                display this help message\n\
  -i, --ip=IPADDRESS        ip address of the remote host\n\
  -o, --output=FILENAME     output file name\n\
  -p, --port=PORT           port to connect to\n\
", out);
    exit(status);
}

int main(int argc, char** argv) {
    /* deal with options */
    program_name = argv[0];
    int c;
    while ( (c = getopt_long(argc, argv, "i:o:p:", long_options, NULL)) != -1 ) {
        switch (c) {
            case 'h':
                print_usage_and_exit(0);
                break;
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
                print_usage_and_exit(1);
                break;
            default: // Shouldn't happen
                fprintf(stderr, "bad option: %c\n", optopt);
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

    // define this above any jumps
    int i_should_free_outfilename = 0;

    struct file_info_s file_info;
    if (get_remote_file_info(&file_info) < 0) {
        log_err("Failed to get information about the remote file");
        goto shutdown;
    }
    debug("Downloading %s", file_info.filename);
    odebug("%d", file_info.blk_size);

    int to_alloc = 512;
    while (to_alloc < 4 * file_info.blk_size) {
        to_alloc = to_alloc << 1;
        if (to_alloc >= 8 * 1024) break;
    }
    netbuf = malloc(to_alloc);
    if (!netbuf) {
        log_err("Failed to allocate the network buffer");
        goto shutdown;
    }
    netbuf_len = to_alloc;

    if (!outfilename) {
        outfilename = strdup(file_info.filename);
        i_should_free_outfilename = 1;
    }

    filesize_in_blocks = file_info.num_blocks;
    // do { get some packets, try to decode } while ( not decoded )
    proc_file(from_network, &file_info);

    // TODO check that this truncate is available in mingw
    // Think we need to do open() then ftruncate(fd) or chsize(fd)
#ifdef _WIN32
    int fd = open(outfilename, O_RDONLY);
    if (fd >= 0 && ftruncate(fd, file_info.filesize) >= 0) {
        close(fd);
    } else
        log_err("Failed to truncate the output file");
#else
    if (truncate(outfilename, file_info.filesize) < 0)
        log_err("Failed to truncate the output file");
#endif // _WIN32

shutdown:
    if (i_should_free_outfilename)
        free(outfilename);
    free(netbuf);
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
        memset(buf, '\0', buf_len);
        bytes_recvd = recvfrom(s, buf, buf_len, 0,
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

int get_remote_file_info(file_info_s* file_info) {
    send_msg(curr_server, MSG_INFO ENDL);
    char buf[BUF_LEN];
    int bytes_recvd = recv_msg(buf, BUF_LEN);
    if (bytes_recvd < 0) return -1;
    struct file_info_s* net_info = (struct file_info_s *) buf;
    if (net_info->magic == MAGIC_INFO) {
        if (net_info->blk_size < 0
                || net_info->num_blocks < 0
                || net_info->filesize < 0) {
            log_err("Corrupt packet");
            return ERR_NETWORK;
        }
        memcpy(file_info, net_info, sizeof *file_info);
        return 0;
    }
    log_err("Packet was not a fileinfo packet");
    return -1;
}

static void ftn_cache_alloc(ftn_cache_s* cache) {
    cache->base = malloc(BURST_SIZE * sizeof *cache->base);
    if (cache->base == NULL) return;

    cache->capacity = BURST_SIZE;
    cache->current = cache->base;
}

static void load_from_network(ftn_cache_s* cache) {
    send_msg(curr_server, MSG_WAITING ENDL);


    // we need to do these either 750 times or... have a timeout
    // we might also want to adjust our yield expectation depending
    // on whether we are hitting those timeouts or not...
    //
    // TODO Next important task, work out how to switch between filling the
    // cache when there are packets and decoding when there aren't
    for (int i = 0; i < 200; i++) {
        int bytes_recvd = recv_msg(netbuf, netbuf_len);
        if (bytes_recvd < 0)
            handle_error(bytes_recvd, NULL);

        buffer_s packet = {
            .length = bytes_recvd,
            .buffer = netbuf
        };
        fountain_s* ftn = unpack_fountain(packet, filesize_in_blocks);
        if (ftn == NULL) { // Checksum may have failed
            // If the system runs out of memory this may become an infinite
            // loop... we could create an int offset instead of using
            // i, but that may end the program if we get too many bad packets.
            // Both are unlikely. May have to consider using some sort of
            // error code instead
            --i;
            continue;
        }
        cache->base[i] = ftn;
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

/* process fountains as they come down the wire
   returns a status code (see errors.c)
 */
int proc_file(fountain_src ftn_src, file_info_s* file_info) {
    int result = 0;
    char * err_str = NULL;

    decodestate_s* state =
        decodestate_new(file_info->blk_size, file_info->num_blocks);
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

    log_info("Total packets required for download: %d", state->packets_so_far);

cleanup:
    fclose(state->fp);
free_state:
    decodestate_free(state);
    return handle_error(result, err_str);
}

