#define _GNU_SOURCE // asks stdio.h to include asprintf
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#include <time.h> //time
#include <unistd.h> //getopt
#ifdef _WIN32
#   include "asprintf.h"
#endif
#include "errors.h"
#include "fountain.h"
#include "dbg.h"

#define LISTEN_PORT 2534
#define LISTEN_IP "127.0.0.1"
#define ENDL "\r\n"
#define BUF_LEN 512
#define BURST_SIZE 1000

// === Types ===
typedef struct client_s {
    struct sockaddr_in address;
} client_s;

/* Forward declarations */
static int create_connection(const char* ip_address);
static int recvd_hello(client_s * new_client);
static void close_connection();
static int send_filename(client_s client, const char * filename);
static int send_std_msg(client_s client, char const * msg);
static int packet_size(fountain_s* ftn);
static char* pack_fountain(fountain_s* ftn);
static int send_fountain(client_s client, fountain_s* ftn);
static int send_block_burst(client_s client, const char * filename);

static SOCKET s;
static WSADATA w;
static int listen_port = LISTEN_PORT;
static char* listen_ip = LISTEN_IP;
static char const * program_name;
static int blk_size = 128; /* better to set this based on filesize */

static void print_usage_and_exit(int status) {
    printf("Usage: %s [OPTION]... FILE\n", program_name);
    fputs("\
\n\
  -b, --blocksize   manually set the blocksize in bytes\n\
  -h, --help        display this help message\n\
  -i, --ip          set the ip address to listen on default is 0.0.0.0\n\
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
            fprintf(stderr, "bad option %s\n", optopt);
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
        if (hello) {
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
    }

    close_connection();
    return 0;
}

int create_connection(const char* ip_address) {
    struct sockaddr_in addr;

    if (WSAStartup(0x0202, &w))
        return -10;
    if (w.wVersion != 0x0202)
        return -20;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return -30;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = inet_addr(ip_address);

    if (bind(s, (struct sockaddr*)&addr, sizeof addr) == SOCKET_ERROR)
        return -40;

    return 0;
}

int recvd_hello(client_s * new_client) {
    char buf[BUF_LEN];
    struct sockaddr_in remote_addr;
    int remote_addr_size = sizeof remote_addr;

    memset(buf, '\0', BUF_LEN);
    if (recvfrom(s, buf, BUF_LEN, 0, (struct sockaddr*)&remote_addr,
                &remote_addr_size) < 0)
        return -1;
    if (strcmp(buf, "FCWAITING" ENDL) == 0) {
        new_client->address = remote_addr;
        return 1;
    }

    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    WSACleanup();
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
    if (asprintf(&msg, "FILENAME %s" ENDL, filename) < 0)
        return ERR_MEM;
    send_std_msg(client, msg);
    free(msg); 
    return 0;
}

int packet_size(fountain_s* ftn) {
    return sizeof *ftn
            + ftn->blk_size
            + ftn->num_blocks * sizeof *ftn->block;
}

char* pack_fountain(fountain_s* ftn) {

    void* packed_ftn = malloc(packet_size(ftn));
    if (!packed_ftn) return NULL;

    memcpy(packed_ftn, ftn, sizeof *ftn);
    memcpy(packed_ftn + sizeof *ftn, ftn->string, ftn->blk_size);
    memcpy(packed_ftn + sizeof *ftn + ftn->blk_size,
            ftn->block,
            ftn->num_blocks * sizeof *ftn->block);

    fountain_s* f_ptr = (fountain_s*) packed_ftn;
    f_ptr->string = packed_ftn + sizeof *ftn;
    f_ptr->block = packed_ftn + sizeof *ftn + ftn->blk_size;

    return (char*) packed_ftn;
}

int send_fountain(client_s client, fountain_s* ftn) {
    char* packet = pack_fountain(ftn);
    if (packet == NULL) return ERR_PACKING;

    int bytes_sent = sendto(s, packet, packet_size(ftn), 0,
            (struct sockaddr*)&client.address,
            sizeof client.address);

    free(packet);

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

