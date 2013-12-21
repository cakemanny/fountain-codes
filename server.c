#define _GNU_SOURCE // asks stdio.h to include asprintf

/* Load in the correct networking libraries for the OS */
#include "networking.h"

#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#include <time.h> //time
#include <unistd.h> //getopt

/* Windows doesn't seem to provide asprintf.h... */
#ifdef _WIN32
#   include "asprintf.h"
#endif
#include "errors.h"
#include "fountain.h"
#include "dbg.h"
#include "fountainprotocol.h" // msg definitions

#define LISTEN_PORT 2534
#define LISTEN_IP "127.0.0.1"
#define ENDL "\r\n" /* For network we always want to use CRLF */
#define BUF_LEN 512
#define BURST_SIZE 1000

// ------ types ------
typedef struct client_s {
    struct sockaddr_in address;
} client_s;


// ------ Forward declarations ------
static int create_connection(const char* ip_address);
static int recvd_hello(client_s * new_client);
static void close_connection();
static int send_filename(client_s client, const char * filename);
static int send_std_msg(client_s client, char const * msg);
static int send_fountain(client_s client, fountain_s* ftn);
static int send_block_burst(client_s client, const char * filename);


// Message lookup table
typedef int (*msg_despatch_f)(client_s /* client */,
                              const char * /* filename */);
struct msg_lookup {
    int id;
    const char *msg;
    msg_despatch_f despatcher;
};

static struct msg_lookup lookup_table[] =
{
    { 0, "" , NULL},
    { 1, MSG_WAITING ENDL, send_block_burst },
    { 2, MSG_SIZE ENDL, NULL },
    { 3, MSG_BLKSIZE ENDL, NULL },
    { 4, MSG_FILENAME ENDL, send_filename },
    { 5, MSG_INFO ENDL, NULL },
    { 6, NULL, NULL}
};

// ------ static variables ------
static SOCKET s;
#ifdef _WIN32
static WSADATA w;
#endif /* _WIN32 */
static int listen_port = LISTEN_PORT;
static char* listen_ip = LISTEN_IP;
static char const * program_name;
static int blk_size = 128; /* better to set this based on filesize */


// ------ functions ------
static void print_usage_and_exit(int status) {
    printf("Usage: %s [OPTION]... FILE\n", program_name);
    fputs("\
\n\
  -b, --blocksize   manually set the blocksize in bytes\n\
  -h, --help        display this help message\n\
  -i, --ip          set the ip address to listen on default is 127.0.0.1\n\
  -p, --port        set the UDP port to listen on, default is 2534\n\
", stdout);
    exit(status);
}

int main(int argc, char** argv) {
    /* deal with options */
    program_name = argv[0];
    int c;
    while ( (c = getopt(argc, argv, "b:hi:p:")) != -1) {
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
            case 'p':
                listen_port = atoi(optarg);
                break;
            case '?':
            fprintf(stderr, "bad option %c\n", optopt);
                break;
        }
    }
    char const * filename;
    if (optind < argc) {
        filename = argv[optind];
    } else {
        print_usage_and_exit(1);
    }

    /* seed random number generation */
    srand(time(NULL));

    // Check that the file exists
    FILE* f = fopen(filename, "r");
    if (f == NULL) handle_error(ERR_FOPEN, &filename);
    fclose(f);

    int error;
    if ((error = create_connection(listen_ip)) < 0) {
        close_connection();
        return -1;
    }
    printf("Listening on %s:%d ..." ENDL,listen_ip, listen_port);

    client_s client;

    int hello;
    while ((hello = recvd_hello(&client)) >= 0) {
        if (hello == 1) {
            printf("Received connection..." ENDL);
            /* == do some file transfering ==
             * send file signature
             * while !recv'd finished: send block
            */
            if ((error = send_filename(client, filename)) < 0)
                handle_error(error, NULL);
            if ((error = send_block_burst(client, filename)) < 0)
                handle_error(error, &filename);
        }
        else if (hello == 2) {
            printf("Sending file info... " ENDL);
        }
        // accept SIZEINBLOCKS request
        // accept BLOCKSIZE request
        // accept FILENAME request
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

//
// Translate the message sent to us

int recvd_hello(client_s * new_client) {
    char buf[BUF_LEN];
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_size = sizeof remote_addr;

    memset(buf, '\0', BUF_LEN);
    if (recvfrom(s, buf, BUF_LEN, 0, (struct sockaddr*)&remote_addr,
                &remote_addr_size) < 0)
        return -1;

    // Lookup the message in the table
    for (int i = 1; lookup_table[i].msg != NULL; i++) {
        if (strcmp(buf, lookup_table[i].msg) == 0) {
            new_client->address = remote_addr;
            return i;
        }
    }
    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    #ifdef _WIN32
    WSACleanup();
    #endif
}

/* Send a message to a client, terminating in \r\n\r\n, rather than a file */

int send_std_msg(client_s client, char const * msg) {
    sendto(s, msg, strlen(msg), 0,
            (struct sockaddr*)&client.address,
            sizeof client.address);
    return 0;
}

int send_filename(client_s client, const char * filename) {
    char * msg;
    if (asprintf(&msg, HDR_FILENAME "%s" ENDL, filename) < 0)
        return ERR_MEM;
    send_std_msg(client, msg);
    free(msg); 
    return 0;
}

int send_fountain(client_s client, fountain_s* ftn) {
    buffer_s packet = pack_fountain(ftn);
    if (packet.length == 0) return ERR_PACKING;

    int bytes_sent = sendto(s, packet.buffer, packet.length, 0,
            (struct sockaddr*)&client.address,
            sizeof client.address);

    free(packet.buffer);

    if (bytes_sent == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

int send_block_burst(client_s client, const char * filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return ERR_FOPEN;
    for (int i = 0; i < BURST_SIZE; i++) {
        // make a fountain
        // send it across the air
        fountain_s* ftn = fmake_fountain(f, blk_size);
        if (ftn == NULL) return ERR_MEM;
        int error = send_fountain(client, ftn);
        if (error < 0) handle_error(error, NULL);
        free_fountain(ftn);
    }
    log_info("Sent packet burst of size %d", BURST_SIZE);

    fclose(f);
    return 0;
}

