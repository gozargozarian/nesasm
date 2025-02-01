#pragma once

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <string.h>

#ifndef _LIBC
# define __strcasecmp strcasecmp
# define TOLOWER(Ch) tolower (Ch)
#else
# include <locale/localeinfo.h>
# ifdef USE_IN_EXTENDED_LOCALE_MODEL
#  define __strcasecmp __strcasecmp_l
# endif
# define TOLOWER(Ch) __tolower_l ((Ch), loc)
#endif

#ifdef USE_IN_EXTENDED_LOCALE_MODEL
# define LOCALE_PARAM , locale_t loc
#else
# define LOCALE_PARAM
#endif

/* Compare S1 and S2, ignoring case, returning less than, equal to or
   greater than zero if S1 is lexicographically less than,
   equal to or greater than S2.  */
int
__strcasecmp(const char* s1, const char* s2 LOCALE_PARAM) {
#if defined _LIBC && !defined USE_IN_EXTENDED_LOCALE_MODEL
    locale_t loc = _NL_CURRENT_LOCALE;
#endif
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    int result;
    if (p1 == p2)
        return 0;
    while ((result = TOLOWER(*p1) - TOLOWER(*p2++)) == 0)
        if (*p1++ == '\0')
            break;
    return result;
}
#ifndef __strcasecmp
libc_hidden_def(__strcasecmp)
weak_alias(__strcasecmp, strcasecmp)
#endif