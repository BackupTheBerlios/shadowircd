/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  s_auth.h: A header for the ident functions.
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
 *  $Id: s_auth.h,v 1.2 2004/09/07 00:03:46 nenolod Exp $
 */

#ifndef INCLUDED_s_auth_h
#define INCLUDED_s_auth_h

#include "res.h"

/* How many auth allocations to allocate in a block. I'm guessing that
 * a good number here is 64, because these are temporary and don't live
 * as long as clients do.
 *     -- adrian
 */
#define	AUTH_BLOCK_SIZE 64

struct Client;

struct AuthRequest
{
  dlink_node	      dns_node;	 /* auth_doing_dns_list */
  dlink_node	      ident_node; /* auth_doing_ident_list */
  int 		      flags;
  struct Client*      client;    /* pointer to client struct for request */
  int                 fd;        /* file descriptor for auth queries */
  time_t              timeout;   /* time when query expires */
  unsigned int	      ip6_int;
};

/*
 * flag values for AuthRequest
 * NAMESPACE: AM_xxx - Authentication Module
 */
#define AM_AUTH_CONNECTING   (1 << 0)
#define AM_AUTH_PENDING      (1 << 1)
#define AM_DNS_PENDING       (1 << 2)

#define SetDNSPending(x)     ((x)->flags |= AM_DNS_PENDING)
#define ClearDNSPending(x)   ((x)->flags &= ~AM_DNS_PENDING)
#define IsDNSPending(x)      ((x)->flags &  AM_DNS_PENDING)

#define SetAuthConnect(x)    ((x)->flags |= AM_AUTH_CONNECTING)
#define ClearAuthConnect(x)  ((x)->flags &= ~AM_AUTH_CONNECTING)
#define IsAuthConnect(x)     ((x)->flags &  AM_AUTH_CONNECTING)

#define SetAuthPending(x)    ((x)->flags |= AM_AUTH_PENDING)
#define ClearAuthPending(x)  ((x)->flags &= AM_AUTH_PENDING)
#define IsAuthPending(x)     ((x)->flags &  AM_AUTH_PENDING)

#define ClearAuth(x)         ((x)->flags &= ~(AM_AUTH_PENDING | AM_AUTH_CONNECTING))
#define IsDoingAuth(x)       ((x)->flags &  (AM_AUTH_PENDING | AM_AUTH_CONNECTING))
/* #define SetGotId(x)       ((x)->flags |= FLAGS_GOTID) */


extern void start_auth(struct Client *);
extern void send_auth_query(struct AuthRequest* req);
extern void remove_auth_request(struct AuthRequest *req);
extern struct AuthRequest *FindAuthClient(long id);
extern void init_auth(void);
extern void delete_identd_queries(struct Client *);

#endif /* INCLUDED_s_auth_h */










