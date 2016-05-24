#ifndef __PREHEADER_H__
#define __PREHEADER_H__

/*
 * We have some useful stuff in here which we were using in various different
 * header files. Makes sense to put in one place
 */

// GAH!! this might break stuff on windows
#if !defined(__clang__) && !defined(__has_builtin)
#   if defined(__GNUC__)
#       define __has_builtin(x) 1
#   else
#       define __has_builtin(x) 0
#   endif
#endif

#ifdef __GNUC__
#   ifndef GCC_VERSION
#       define GCC_VERSION (__GNUC__ * 10000 \
                            + __GNUC_MINOR__ * 100 \
                            + __GNUC_PATCHLEVEL__)
#   endif
#endif

#endif
