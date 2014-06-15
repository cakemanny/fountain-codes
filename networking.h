#ifndef __NETWORKING_H__
#define __NETWORKING_H__

/* Load in the correct networking libraries for the OS */
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
//#   include <mstcpip.h> /* To use WSAPoll -- MISSING on my system*/

/* 32-bit MinGW does not appear to define these anywhere */
#   ifndef POLLRDNORM /* Incase they are defined yon your system */

#       define POLLRDNORM 0x0100
#       define POLLRDBAND 0x0200
#       define POLLIN    (POLLRDNORM | POLLRDBAND)
#       define POLLPRI    0x0400

#       define POLLWRNORM 0x0010
#       define POLLOUT   (POLLWRNORM)
#       define POLLWRBAND 0x0020

#       define POLLERR    0x0001
#       define POLLHUP    0x0002
#       define POLLNVAL   0x0004

typedef struct pollfd {
  SOCKET fd;
  short  events;
  short  revents;
} WSAPOLLFD, *PWSAPOLLFD, *LPWSAPOLLFD;

int WSAAPI WSAPoll(
  WSAPOLLFD fdarray[],
  ULONG nfds,
  INT timeout
);

#   endif
#   define poll WSAPoll
#else
#   define closesocket close
#   include <sys/types.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <poll.h> /* Included here since the win32 counterpart is also */
#endif

/* Here we define the constant names used in the windows libraries so that
 we can use those names for both. They are in fact a bit  more descriptive
 than using the return values straight out of the manual */

/* define SOCKET as int for unix */
#ifndef SOCKET
#   define SOCKET int
#endif /* SOCKET */

/* define SOCKET_ERROR as -1 */
#ifndef SOCKET_ERROR
#   define SOCKET_ERROR -1
#endif
/* define INVALID_SOCKET as -1 for unix people */
#ifndef INVALID_SOCKET
#   define INVALID_SOCKET -1
#endif

#endif /* __NETWORKING_H__ */
