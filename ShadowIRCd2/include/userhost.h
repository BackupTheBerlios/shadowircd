/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  userhost.h: A header for global user limits.
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
 *  $Id: userhost.h,v 1.1.1.1 2003/12/02 20:47:56 nenolod Exp $
 */

#ifndef INCLUDED_userhost_h
#define INCLUDED_userhost_h

struct NameHost
{
  dlink_node node;		/* point to other names on this hostname */
  char name[USERLEN + 1];
  int icount;			/* number of =local= identd on this name*/
  int gcount;			/* global user count on this name */
  int lcount;			/* local user count on this name */
};

struct UserHost
{
  dlink_list list;		/* list of names on this hostname */
  struct UserHost *next;
  char host[HOSTLEN + 1];
};

extern void
    count_user_host(const char *user, const char *host,
		    int *global_p, int *local_p, int *icount_p);
extern void add_user_host(char *user, const char *host, int global);
void delete_user_host(char *user, const char *host, int global);


#endif  /* INCLUDED_userhost_h */
