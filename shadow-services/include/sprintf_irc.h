/*
 * $Id: sprintf_irc.h,v 1.1 2003/12/16 19:52:39 nenolod Exp $ 
 */

#ifndef SPRINTF_IRC
#define SPRINTF_IRC

#include <stdarg.h>

/* Prototypes */

extern int vsprintf_irc(register char *str, register const char *format,
    va_list);
extern int ircsprintf(register char *str, register const char *format, ...);

#endif /* SPRINTF_IRC */
