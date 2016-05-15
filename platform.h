#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#ifdef _WIN32
#   define IFWIN32(x,y)     x
#else
#   define IFWIN32(x,y)     y
#endif

#ifdef __GNUC__
#define __malloc    __attribute__((malloc))
#else
#define __malloc    /* */
#endif // __GNUC__

#endif /* __PLATFORM_H__ */
