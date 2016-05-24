#ifndef HAVE_STPCPY
#define HAVE_STPCPY

// Don't bother checking for builtin as we will only have this missing on win32

#include <string.h> //memcpy
static char* stpcpy(char* dst, const char* src)
{
    size_t len = strlen(src);
    return memcpy(dst, src, len + 1) + len;
}

#endif
