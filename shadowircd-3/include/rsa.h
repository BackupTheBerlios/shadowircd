/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  rsa.h: A header for the RSA functions.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
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
 *  $Id: rsa.h,v 3.3 2004/09/08 01:18:07 nenolod Exp $
 */
#ifndef INCLUDED_rsa_h
#define INCLUDED_rsa_h

#include "setup.h"

#ifdef HAVE_LIBCRYPTO
extern void report_crypto_errors(void);
extern int verify_private_key(void);
extern int generate_challenge( char ** r_challenge, char ** r_response, RSA * key );
extern int get_randomness( unsigned char * buf, int length );
#endif

#endif /* INCLUDED_rsa_h */

