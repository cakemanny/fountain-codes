#ifndef HAVE_STPCPY
#define HAVE_STPCPY

#include <string.h> //memcpy

#undef stpcpy

static char* stpcpy(char* dst, const char* src)
{
    size_t len = strlen(src);
    return memcpy(dst, src, len + 1) + len;
}

#endif
