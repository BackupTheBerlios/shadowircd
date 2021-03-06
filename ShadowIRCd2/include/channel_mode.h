/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  channel_mode.h: The ircd channel mode header.
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
 *  $Id: channel_mode.h,v 1.14 2004/04/07 19:24:22 nenolod Exp $
 */


#ifndef INCLUDED_channel_mode_h
#define INCLUDED_channel_mode_h

#include "ircd_defs.h"        /* buffer sizes */

#define MODEBUFLEN    200

/* Maximum mode changes allowed per client, per server is different */
#define MAXMODEPARAMS 4

extern int add_id(struct Client *, struct Channel *, char *, int);
extern void set_channel_mode(struct Client *, struct Client *, struct Channel *,
                             struct Membership *, int, char **, char *);

extern void init_chcap_usage_counts(void);
extern void set_chcap_usage_counts(struct Client *serv_p);
extern void unset_chcap_usage_counts(struct Client *serv_p);

/*
** Channel Related macros follow
*/

/* can_send results */
#define CAN_SEND_NO	0
#define CAN_SEND_NONOP  1
#define CAN_SEND_OPV	2


/* Channel related flags */
#define CHFL_CHANOP     0x0001 /* Channel operator   */
#define CHFL_HALFOP     0x0002 /* Channel half op    */
#define CHFL_VOICE      0x0004 /* the power to speak */
#define CHFL_DEOPPED    0x0008 /* deopped by us, modes need to be bounced */
#define CHFL_BAN        0x0010 /* ban channel flag */
#define CHFL_EXCEPTION  0x0020 /* exception to ban channel flag */
#define CHFL_INVEX      0x0040 /* Invite exception. */
#define CHFL_QUIET	0x0080 /* Quiet. */
#define CHFL_CHANOWNER  0x0100 /* Channel owner      */
#define CHFL_RESTRICT   0x0200 /* Restricted user    */

/* channel modes ONLY */
#define MODE_PRIVATE    0x00000001
#define MODE_SECRET     0x00000002
#define MODE_MODERATED  0x00000004
#define MODE_TOPICLIMIT 0x00000008
#define MODE_INVITEONLY 0x00000010
#define MODE_NOPRIVMSGS 0x00000020
#define MODE_NOCOLOR	0x00000040
#define MODE_STICKYNICK 0x00000080
#define MODE_STRIPCOLOR 0x00000100
#define MODE_NOKNOCK    0x00000200
#define MODE_NOTHROTTLE 0x00000400
#define MODE_PEACE	0x00000800
#define MODE_TOPICLOCK  0x00001000
#define MODE_FREEINVITE 0x00002000
#define MODE_REGONLY    0x00004000
#define MODE_OPERONLY   0x00008000
#define MODE_OPMODERATE 0x00010000
#define MODE_REGISTERED 0x00020000
#define MODE_AUDITORIUM 0x00040000
#define MODE_FILTER     0x00080000 /* Only because it was requested by many. */

#define MODE_QUERY  0
#define MODE_ADD    1
#define MODE_DEL   -1

/* name invisible */
#define SecretChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define HiddenChannel(x)        ((x) && ((x)->mode.mode & MODE_PRIVATE))
#define PubChannel(x)           ((!x) || ((x)->mode.mode &\
                                 (MODE_PRIVATE | MODE_SECRET)) == 0)
#define ParanoidChannel(x)	((x) && ((x)->mode.mode &\
			        (MODE_PRIVATE|MODE_INVITEONLY))==\
		                (MODE_PRIVATE|MODE_INVITEONLY))

struct ChModeChange
{
  char letter;
  char *arg;
  char *id;
  int dir;
  int caps;
  int nocaps;
  int mems;
  struct Client *client;
};

struct ChCapCombo
{
  int count;
  int cap_yes;
  int cap_no;
};

#define CHACCESS_NOTONCHAN  -1
#define CHACCESS_PEON       0
#define CHACCESS_HALFOP     1
#define CHACCESS_CHANOP     2
#define CHACCESS_CHANOWNER  3
#endif /* INCLUDED_channel_mode_h */
