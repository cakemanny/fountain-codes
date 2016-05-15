#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#ifdef _WIN32
#   define IFWIN32(x,y)     x
#else
#   define IFWIN32(x,y)     y
#endif

#endif /* __PLATFORM_H__ */
