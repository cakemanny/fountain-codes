#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#define _GNU_SOURCE // asks stdio.h to inlude asprintf
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fountain.h"

#define LISTEN_PORT 2534
#define LISTEN_IP "127.0.0.1"
#define ENDL "\r\n"
#define BUF_LEN 512
#define BURST_SIZE 1000

/* Forward declarations */
static int create_connection(const char* ip_address);
static int recvd_hello();
static void close_connection();
static int send_filename(const char * filename);
static void handle_error(int error_number);
static void send_block_burst(const char * filename);

static SOCKET s;
static WSADATA w;

void print_usage_and_exit() {
    printf("usage: serve ipaddress");
    exit(0);
}

int main(int argc, char** argv) {
    int error, hello;

    if ((error = create_connection(LISTEN_IP)) < 0) {
        close_connection();
        return -1;
    }
    printf("Listening on " LISTEN_IP ":%d ..." ENDL, LISTEN_PORT);

    while ((hello = recvd_hello()) >= 0) {
        if (hello) {
            printf("Received connection...\n");
            /* == do some file transfering ==
             * send file signature
             * while !recv'd finished: send block
            */
            if ((error = send_filename()) < 0)
                handle_error(error);
            send_block_burst();
            }
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

int recvd_hello() {
    char buf[BUF_LEN];
    struct sockaddr_in remote_addr;
    int remote_addr_size = sizeof remote_addr;

    memset(buf, '\0', BUF_LEN);
    if (recvfrom(s, buf, BUF_LEN, 0, (struct sockaddr*)&remote_addr,
                &remote_addr_size) < 0)
        return -1;
    if (strcmp(buf, "FCWAITING\r\n") == 0) {
        return 1;
    }

    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    WSACleanup();
}

int send_filename(const char * filename) {
    // do some dort of sendto(s buf buf_len ...
}

void handle_error(int error_number) {
    switch (error_number) {
        case -1:
            printf("Error: " ENDL);
            break;
    }
}

void send_block_burst(const char * filename) {
    for (int i = 0; i < BURST_SIZE; i++) {
        // read a bit of file
        // make a fountain
        // send it across the air
    }
}

