/*
 *  UltimateIRCd - an Internet Relay Chat Daemon, include/sock.h
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
 *  Refer to the documentation within doc/authors/ for full credits and copyrights.
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
 *  $Id: sock.h,v 1.1 2003/11/04 07:11:56 nenolod Exp $
 *
 */

#ifndef FD_ZERO
# define FD_ZERO(set)      (((set)->fds_bits[0]) = 0)
# define FD_SET(s1, set)   (((set)->fds_bits[0]) |= 1 << (s1))
# define FD_ISSET(s1, set) (((set)->fds_bits[0]) & (1 << (s1)))
# define FD_SETSIZE        30
#endif
