#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LISTEN_PORT 2534
#define BUF_LEN 512

/* Forward declarations */
static int create_connection(const char* ip_address);
static int recvd_hello();
static void close_connection();

static SOCKET s;
static WSADATA w;

void print_usage_and_exit() {
    printf("usage: serve ipaddress");
    exit(0);
}

int main(int argc, char** argv) {
    int error, hello;
    if ((error = create_connection("127.0.0.1")) < 0) {
        close_connection();
        return -1;
    }

    while ((hello = recvd_hello()) >=0) {
        if (hello) {
            /* do some file transfering
             * send file signature
             * while !recv'd finished: send block
            */
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
    if (recvfrom(s, buf, BUF_LEN,(struct sockaddr*)&remote_addr,
                remote_addr_size) < 0)
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


