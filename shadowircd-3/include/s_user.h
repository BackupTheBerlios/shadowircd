/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  s_user.h: A header for the user functions.
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
 *  $Id: s_user.h,v 3.3 2004/09/08 01:18:07 nenolod Exp $
 */

#ifndef INCLUDED_s_user_h
#define INCLUDED_s_user_h

#define IRC_MAXSID 3
#define IRC_MAXUID 6
#define TOTALSIDUID (IRC_MAXSID + IRC_MAXUID)

#include "umodes.h"

struct User;
struct Client;
struct AccessItem;

extern int MaxClientCount;     /* GLOBAL - highest number of clients     */
extern int MaxConnectionCount; /* GLOBAL - highest number of connections */

extern void init_user(void);
extern void free_user(struct User *, struct Client *);
extern void set_user_mode(struct Client *, struct Client *, int, char **);
extern void send_umode(struct Client *, struct Client *,
                       user_modes, unsigned int, char *);
extern void send_umode_out(struct Client *, struct Client *, user_modes *);
extern void show_lusers(struct Client *);
extern void show_isupport(struct Client *);
extern void oper_up(struct Client *);

extern int register_local_user(struct Client *, struct Client *,
                               const char *, const char *);
extern int register_remote_user(struct Client *, struct Client *,
                                const char *, const char *,
                                const char *, const char *,
                                const char *, const char *);
extern int do_local_user(const char *, struct Client *, struct Client *,
                         const char *, const char *, const char *,
                         const char *);
extern int user_modes_from_c_to_bitmask[];
extern void init_uid(void);
extern char *uid_get(void);
#endif

