#define _GNU_SOURCE // asks stdio.h to include asprintf

/* Load in the correct networking libraries for the OS */
#include "networking.h"

#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#include <inttypes.h>
#include <time.h> //time
#include <unistd.h> //getopt
#include <getopt.h> //getopt_long
#include <sys/stat.h>

/* Windows doesn't seem to provide asprintf.h... */
#ifdef _WIN32
#   include "asprintf.h"
#endif
#include "errors.h"
#include "fountain.h"
#include "dbg.h"
#include "fountainprotocol.h" // msg definitions

#define LISTEN_PORT 2534
#define LISTEN_IP "0.0.0.0"
#define BUF_LEN 512
#define BURST_SIZE 1000

// ------ types ------
typedef struct client_s {
    struct sockaddr_in address;
} client_s;


// ------ Forward declarations ------
static int create_connection(const char* ip_address);
static int receive_request(client_s * new_client, const char * filename);
static void close_connection();
static int send_fountain(client_s * client, fountain_s* ftn);
static int send_block_burst(client_s * client, const char * filename, int capacity);
static int send_info(client_s * client, const char * filename);
static int filesize_in_bytes(const char * filename);



// TODO: test use of long options on windows
struct option long_options[] = {
    { "blocksize",  required_argument, NULL, 'b' },
    { "help",       no_argument,       NULL, 'h' },
    { "ip",         required_argument, NULL, 'i' },
    { "latency",    required_argument, NULL, 'L' },
    { "port",       required_argument, NULL, 'p' },
    { 0, 0, 0, 0 }
};

// ------ static variables ------
static SOCKET s;
#ifdef _WIN32
static WSADATA w;
#endif /* _WIN32 */
static int listen_port = LISTEN_PORT;
static char* listen_ip = LISTEN_IP;
static char const * program_name;
static int blk_size = -1; /* better to set this based on filesize */

static int dbg_add_response_latency = 0;

// ------ functions ------
static void print_usage_and_exit(int status) {
    FILE* out = (status == 0) ? stdout : stderr;

    fprintf(out, "Usage: %s [OPTION]... FILE\n", program_name);
    fputs("\
\n\
  -b, --blocksize=BYTES     manually set the blocksize in bytes\n\
  -h, --help                display this help message\n\
  -i, --ip=IPADDRESS        set the ip address to listen on, the default is \n\
                              0.0.0.0\n\
  -L, --latency=LATENCY     debug setting: adds response latency to the server\n\
  -p, --port=PORT           set the UDP port to listen on, default is 2534\n\
", out);
    exit(status);
}

int main(int argc, char** argv) {
    /* deal with options */
    program_name = argv[0];
    int c;
    while ( (c = getopt_long(argc, argv, "b:hi:L:p:", long_options, NULL)) != -1) {
        switch (c) {
            case 'b':
                blk_size = atoi(optarg);
                break;
            case 'h':
                print_usage_and_exit(0);
                break;
            case 'i':
                listen_ip = optarg;
                break;
            case 'L':
                dbg_add_response_latency = atoi(optarg);
                break;
            case 'p':
                listen_port = atoi(optarg);
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
    char const * filename = NULL;
    if (optind < argc) {
        filename = argv[optind];
    } else {
        print_usage_and_exit(1);
    }

    /* seed random number generation */
    srand(time(NULL));

    // Check that the file exists
    FILE* f = fopen(filename, "rb");
    if (f == NULL) handle_error(ERR_FOPEN, &filename);
    fclose(f);

    if (blk_size <= 0) {
        blk_size = 128; // Should probably start at 1024
        int filesize = filesize_in_bytes(filename);
        if (filesize < 0)
            return -1;
        while (filesize / blk_size > INT16_MAX) {
            blk_size <<= 1;
        }
        odebug("%d", blk_size);
    } else if (filesize_in_bytes(filename) / blk_size > INT16_MAX) {
        /*  The user provided a blk_size... better check they haven't done  *
         *  a silly                                                         */
        log_err("Block size is too small. Cannot divide file "
                "into %d pieces or more", INT16_MAX);
        return -1;
    } else if (blk_size > MAX_BLOCK_SIZE) {
        log_err("Maximum block size is %d", MAX_BLOCK_SIZE);
        return -1;
    }

    int error;
    if ((error = create_connection(listen_ip)) < 0) {
        log_err("Unable to bind to socket");
        close_connection();
        return -1;
    }
    printf("Listening on %s:%d ...\n" ,listen_ip, listen_port);

    client_s client;

    int request_type;
    while ((request_type = receive_request(&client, filename)) >= 0) {
        //
    }

    close_connection();
    return 0;
}

int create_connection(const char* ip_address) {
    struct sockaddr_in addr;

    #ifdef _WIN32
    if (WSAStartup(0x0202, &w))
        return -10;
    if (w.wVersion != 0x0202)
        return -20;
    #endif

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return -30;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = inet_addr(ip_address);

    if (bind(s, (struct sockaddr*)&addr, sizeof addr) < 0)
        return -40;

    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    #ifdef _WIN32
    WSACleanup();
    #endif
}


static void wait_signal_order_from_network(wait_signal_s* wait_signal) {
    wait_signal->magic = htonl(wait_signal->magic);
    wait_signal->capacity = htonl(wait_signal->capacity);
}
//
// Translate the message sent to us

int receive_request(client_s * new_client, const char * filename) {
    char buf[BUF_LEN];
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_size = sizeof remote_addr;

    memset(buf, '\0', BUF_LEN);
    if (recvfrom(s, buf, BUF_LEN, 0, (struct sockaddr*)&remote_addr,
                &remote_addr_size) < 0)
        return -1;

    new_client->address = remote_addr;

    debug("Received msg: %s", buf);

#ifndef NDEBUG
    // This doesn't actually simulate latency
    // Really we need a receive buffer
    if (dbg_add_response_latency) {
        debug("Sleeping for %d seconds...", dbg_add_response_latency);
        sleep(dbg_add_response_latency);
    }
#endif

    // Lookup the message in the table
    packet_s* packet = (packet_s*)buf;
    int magic = ntohl(packet->magic);

    int error;

    switch (magic) {
        case MAGIC_REQUEST_INFO:
            error = send_info(new_client, filename);
            break;
        case MAGIC_WAITING:
            {
                wait_signal_s* signal = (wait_signal_s*)buf;
                wait_signal_order_from_network(signal);
                error = send_block_burst( new_client, filename, signal->capacity);
            }
            break;
        default:
            return -1;
    }

    if (error < 0)
        handle_error(error, NULL);
    return 0;
}


static void file_info_order_for_network(file_info_s* info) {
    info->magic = htonl(info->magic);
    info->blk_size = htons(info->blk_size);
    info->num_blocks = htons(info->num_blocks);
    info->filesize = htonl(info->filesize);
}

int filesize_in_bytes(const char * filename) {
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    log_err("Error getting filesize");
    return -1;
}

static int fsize_in_blocks (const char * filename) {
    int filesize = filesize_in_bytes(filename);
    if (filesize < 0) return filesize;

    return (filesize % blk_size)
        ? (filesize /blk_size) + 1 : filesize / blk_size;
}

/* This is the size in blocks not the actual filesize we are sending... */


int send_info(client_s * client, const char * filename) {
    debug("Sending info for file %s", filename);

    file_info_s info = {
        .magic      = MAGIC_INFO,
        .blk_size   = blk_size,
        .num_blocks = fsize_in_blocks(filename),
        .filesize   = filesize_in_bytes(filename)
    };

    strncpy(info.filename, filename, sizeof info.filename - 1);
#ifdef _WIN32
    /* Make the path cross platform by replacing \ path separator with / */
    char* bs_pos = info.filename;
    while ((bs_pos = strchr(bs_pos, '\\')) != NULL) {
        *bs_pos++ = '/';
    }
#endif

    odebug("%"PRId16, info.blk_size);
    odebug("%"PRId16, info.num_blocks);
    odebug("%"PRId32, info.filesize);

    file_info_order_for_network(&info);

    int bytes_sent = sendto(s, (char*)&info, sizeof info, 0,
            (struct sockaddr*)&client->address,
            sizeof client->address);

    if (bytes_sent == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

int send_fountain(client_s * client, fountain_s* ftn) {
    buffer_s packet = pack_fountain(ftn);
    if (packet.length == 0) return ERR_PACKING;

    int bytes_sent = sendto(s, packet.buffer, packet.length, 0,
            (struct sockaddr*)&client->address,
            sizeof client->address);

    free(packet.buffer);

    if (bytes_sent == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

int send_block_burst(client_s * client, const char * filename, int capacity) {
    FILE* f = fopen(filename, "rb");
    if (!f) return ERR_FOPEN;
    for (int i = 0; i < capacity; i++) {
        // make a fountain
        // send it across the air
        fountain_s* ftn = fmake_fountain(f, blk_size);
        if (ftn == NULL) return ERR_MEM;
        int error = send_fountain(client, ftn);
        if (error < 0) handle_error(error, NULL);
        free_fountain(ftn);
    }
    log_info("Sent packet burst of size %d", capacity);

    fclose(f);
    return 0;
}

