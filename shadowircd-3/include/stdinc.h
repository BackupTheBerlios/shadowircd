/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  stdinc.h: Pull in all of the necessary system headers
 *
 *  Copyright (C) 2002 Aaron Sethman <androsyn@ratbox.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 * $Id: stdinc.h,v 3.3 2004/09/08 01:18:07 nenolod Exp $
 *
 */

#ifndef STDINC_H /* prevent multiple #includes */
#define STDINC_H
 
#ifndef IN_AUTOCONF
#include "setup.h"
#endif

#include "defaults.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef STRING_WITH_STRINGS
# include <string.h>
# include <strings.h>
#else
# ifdef HAVE_STRING_H
#  include <string.h>
# else
#  ifdef HAVE_STRINGS_H
#   include <strings.h>
#  endif
# endif 
#endif  

#ifdef HAVE_STRTOK_R
# define strtoken(x, y, z) strtok_r(y, z, x)
#endif

#include <sys/types.h>

#ifdef HAVE_STDDEF_H
# include <stddef.h>
#else /* This is basically what stddef.h provides on most systems */
# ifndef NULL
#  define NULL ((void*)0)
# endif
# ifndef offsetof
#  define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
# endif
#endif

#ifdef HAVE_LIBCRYPTO
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#endif

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <fcntl.h>

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif

#include <stdarg.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>

#ifndef _WIN32
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#else
#include <io.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif


#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif


#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifndef _WIN32
#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
extern int errno;
#endif
#else
#define errno WSAGetLastError();
#endif

#ifdef HAVE_LIBCRYPTO
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "inet_misc.h"

extern char ircd_version[512];

#endif
