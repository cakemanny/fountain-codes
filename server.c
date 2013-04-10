#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LISTEN_PORT 2534

/* Forward declarations */
static void close_connection();
static int create_connection(const char* ip_address);

static SOCKET s;
static WSADATA w;

int main(int argc, char** argv) {
    int error;
    if ((error = create_connection("127.0.0.1")) < 0) {
        WSACleanup();
        close_connection();
        return -1;
    }
    
    close_connection();
    return 0;
}

int create_connection(const char* ip_address) {
    if (WSAStartup(0x0202, &w))
        return -10;
    if (w.wVersion != 0x0202) 
        return -20;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return -30;

    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = inet_addr(ip_address);

    if (bind(s, (struct sockaddr*)&addr, sizeof addr) == SOCKET_ERROR)
        return -40;
    if (listen(s, SOMAXCON) == SOCKET_ERROR)
        return -50;

exit:
    return -1;
}

void close_connection() {
    if (s)
        closesocket(s);
    WSACleanup();
}


