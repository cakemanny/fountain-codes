#define _GNU_SOURCE // asks stdio.h to include asprintf
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asprintf.h"
#include "errors.h"
#include "fountain.h"

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
static int send_block_burst(const char * filename);

static SOCKET s;
static WSADATA w;
static char const * program_name;

void print_usage_and_exit(int status) {
    printf("Usage: %s [OPTION]... FILE\n", program_name);
    fputs("\
\n\
  -h, --help        display this help message\n\
  -i, --ip          set the ip address to listen on default is 0.0.0.0\n\
  -p, --port        set the UDP port to listen on, default is 2534\n\
", stdout);
    exit(status);
}

int main(int argc, char** argv) {
    program_name = argv[0];
    if (argc != 2) {
        print_usage_and_exit(1);
    }
    char const * filename = argv[1];

    // Check that the file exists
    FILE* f = fopen(filename, "r");
    if (f == NULL) handle_error(ERR_FOPEN, &filename);
    fclose(f);

    int error;
    if ((error = create_connection(LISTEN_IP)) < 0) {
        close_connection();
        return -1;
    }
    printf("Listening on " LISTEN_IP ":%d ..." ENDL, LISTEN_PORT);

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
            if ((error = send_block_burst(filename)) < 0)
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
    addr.sin_port = htons(LISTEN_PORT);
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

int send_block_burst(const char * filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return ERR_FOPEN;
    for (int i = 0; i < BURST_SIZE; i++) {
        
        // make a fountain
        // send it across the air
    }

    fclose(f);
    return 0;
}

