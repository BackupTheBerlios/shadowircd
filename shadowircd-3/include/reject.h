/*
 *  ircd-ratbox: A slightly useful ircd
 *  reject.h: header to a file which rejects users with prejudice
 *
 *  Copyright 2003 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright 2003-2004 ircd-ratbox development team
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
 *
 *  $Id: reject.h,v 3.3 2004/09/08 01:18:07 nenolod Exp $
 */
#ifndef INCLUDED_reject_h
#define INCLUDED_reject_h

/* amount of time to delay a rejected clients exit */
#define DELAYED_EXIT_TIME	10

void init_reject(void);
int check_reject(struct Client *);
void add_reject(struct Client *);
void flush_reject(void);

extern unsigned long reject_count;

#endif

