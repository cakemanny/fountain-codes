#ifndef HAVE_STPCPY
#define HAVE_STPCPY

#undef stpcpy
#if __have_builtin(__builtin_stpcpy) || defined(__GNUC__)
#   define stpcpy   __builtin_stpcpy
#else

#include <string.h> //memcpy
static char* stpcpy(char* dst, const char* src)
{
    size_t len = strlen(src);
    return memcpy(dst, src, len + 1) + len;
}

#endif

#endif
