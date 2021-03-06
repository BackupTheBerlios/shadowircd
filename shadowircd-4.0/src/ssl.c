/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  ssl.c: Client ssl connections.
 *
 *  Copyright (C) 2003 by the past and present ircd coders, and others.
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
 * $Id: ssl.c,v 1.1 2004/07/29 20:29:15 nenolod Exp $
 *
 */

#include "stdinc.h"
#include "common.h"
#include "s_conf.h"
#include "db.h"
#include "send.h"
#include "s_bsd.h"

#define SAFE_SSL_READ  1
#define SAFE_SSL_WRITE  2
#define SAFE_SSL_ACCEPT  3

#ifdef HAVE_LIBCRYPTO

static int fatal_ssl_error(int, int, struct Client *);

int
safe_SSL_read(struct Client *acptr, void *buf, int sz)
{
	int len, ssl_err;

	len = SSL_read(acptr->ssl, buf, sz);

	if(len <= 0) {
		switch (ssl_err = SSL_get_error(acptr->ssl, len)) {
		case SSL_ERROR_SYSCALL:
			if(errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
		case SSL_ERROR_WANT_READ:
				errno = EWOULDBLOCK;
				return -1;
			}
		case SSL_ERROR_SSL:
			if(errno == EAGAIN)
				return -1;
		default:
			return fatal_ssl_error(ssl_err, SAFE_SSL_READ, acptr);
		}
	}
	return len;
}

int
safe_SSL_write(struct Client *acptr, const void *buf, int sz)
{
	int len, ssl_err;

	len = SSL_write(acptr->ssl, buf, sz);
	if(len <= 0) {
		switch (ssl_err = SSL_get_error(acptr->ssl, len)) {
		case SSL_ERROR_SYSCALL:
			if(errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
		case SSL_ERROR_WANT_WRITE:
				errno = EWOULDBLOCK;
				return 0;
			}
		case SSL_ERROR_SSL:
			if(errno == EAGAIN)
				return 0;
		default:
			return fatal_ssl_error(ssl_err, SAFE_SSL_WRITE, acptr);
		}
	}
	return len;
}

int
safe_SSL_accept(struct Client *acptr, int fd)
{

	int ssl_err;

	if((ssl_err = SSL_accept(acptr->ssl)) <= 0) {
		switch (ssl_err = SSL_get_error(acptr->ssl, ssl_err)) {
		case SSL_ERROR_SYSCALL:
			if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
				/*
				 * handshake will be completed later . . 
				 */
				return 1;
		default:
			return fatal_ssl_error(ssl_err, SAFE_SSL_ACCEPT, acptr);
		}
		/*
		 * NOTREACHED 
		 */
		return 0;
	}
	return 1;
}

int
SSL_smart_shutdown(SSL * ssl)
{
	int i;
	int rc;

	rc = 0;
	for (i = 0; i < 4; i++) {
		if((rc = SSL_shutdown(ssl)))
			break;
	}

	return rc;
}

static int
fatal_ssl_error(int ssl_error, int where, struct Client *sptr)
{
	/*
	 * don't alter errno 
	 */
	int errtmp = errno;
	char *errstr = strerror(errtmp);
	char *ssl_errstr, *ssl_func;

	switch (where) {
	case SAFE_SSL_READ:
		ssl_func = "SSL_read()";
		break;
	case SAFE_SSL_WRITE:
		ssl_func = "SSL_write()";
		break;
	case SAFE_SSL_ACCEPT:
		ssl_func = "SSL_accept()";
		break;
	default:
		ssl_func = "undefined SSL func [this is a bug]";
	}

	switch (ssl_error) {
	case SSL_ERROR_NONE:
		ssl_errstr = "No error";
		break;
	case SSL_ERROR_SSL:
		ssl_errstr = "Internal OpenSSL error or protocol error";
		break;
	case SSL_ERROR_WANT_READ:
		ssl_errstr = "OpenSSL functions requested a read()";
		break;
	case SSL_ERROR_WANT_WRITE:
		ssl_errstr = "OpenSSL functions requested a write()";
		break;
	case SSL_ERROR_WANT_X509_LOOKUP:
		ssl_errstr = "OpenSSL requested a X509 lookup which didn`t arrive";
		break;
	case SSL_ERROR_SYSCALL:
		ssl_errstr = "Underlying syscall error";
		break;
	case SSL_ERROR_ZERO_RETURN:
		ssl_errstr = "Underlying socket operation returned zero";
		break;
	case SSL_ERROR_WANT_CONNECT:
		ssl_errstr = "OpenSSL functions wanted a connect()";
		break;
	default:
		ssl_errstr = "Unknown OpenSSL error (huh?)";
	}

	errno = errtmp ? errtmp : EIO;	/* Stick a generic I/O error */
	sptr->flags |= FLAGS_DEADSOCKET;
	return 0;
}

char *
ssl_get_cipher(SSL *ssl)
{
        static char buf[400];
        buf[0] = '\0';
	ircsprintf(buf, "%s/%s encryption",
		SSL_get_version(ssl), SSL_get_cipher(ssl)); 
        return (buf);
}
#endif
