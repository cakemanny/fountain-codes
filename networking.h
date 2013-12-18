
/* Load in the correct networking libraries for the OS */
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
#else
#   define closesocket close
#   include <sys/types.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
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


