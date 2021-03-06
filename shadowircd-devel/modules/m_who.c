/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_who.c: Shows who is on a channel.
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
 *  $Id: m_who.c,v 1.2 2004/07/12 13:02:16 nenolod Exp $
 */
#include "stdinc.h"
#include "tools.h"
#include "common.h"   
#include "handlers.h"
#include "client.h"
#include "channel.h"
#include "channel_mode.h"
#include "hash.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void m_who(struct Client *, struct Client *, int, char **);
static void ms_who(struct Client *, struct Client *, int, char **);

struct Message who_msgtab = {
  "WHO", 0, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_who, ms_who, m_who, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&who_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&who_msgtab);
}

const char *_version = "$Revision: 1.2 $";
#endif

static void who_global(struct Client *source_p, char *mask, int server_oper);
static void do_who(struct Client *source_p, struct Client *target_p,
                   const char *chname, const char *op_flags);
static void do_who_on_channel(struct Client *source_p, struct Channel *chptr,
                              const char *chname, int member, int server_oper);

/*
** m_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
static void
m_who(struct Client *client_p, struct Client *source_p,
      int parc, char *parv[])
{
  struct Client *target_p;
  char *mask = parv[1];
  dlink_node *lp;
  dlink_node *lp_next;
  struct Channel *chptr = NULL;
  struct Channel *mychannel = NULL;
  int server_oper = parc > 2 ? (*parv[2] == 'o') : 0; /* Show OPERS only */
  int member;
  const char *from, *to;

  if (!MyConnect(source_p) && IsCapable(source_p->from, CAP_TS6) && HasID(source_p))
  {
    from = me.id;
    to = source_p->id;
  }
  else
  {
    from = me.name;
    to = source_p->name;
  }

  /* See if mask is there, collapse it or return if not there */
  if (mask != NULL)
  {
    collapse(mask);

    if (*mask == '\0')
    {
      sendto_one(source_p, form_str(RPL_ENDOFWHO),
                 from, to, "*");
      return;
    }
  }
  else
  {
    who_global(source_p, mask, server_oper);
    sendto_one(source_p, form_str(RPL_ENDOFWHO),
               from, to, "*");
    return;
  }

  /* mask isn't NULL at this point. repeat after me... -db */

  /* '/who *' */
  if ((*(mask + 1) == '\0') && (*mask == '*'))
  {
    if (source_p->user != NULL)
      if ((lp = source_p->user->channel.head) != NULL)
        mychannel = ((struct Membership *)lp->data)->chptr;

    if (mychannel == NULL)
    {
      sendto_one(source_p, form_str(RPL_ENDOFWHO), from, to, "*");
      return;
    }

    do_who_on_channel(source_p, mychannel, mychannel->chname, YES, server_oper);
    sendto_one(source_p, form_str(RPL_ENDOFWHO), from, to, "*");
    return;
  }

  /* '/who #some_channel' */
  if (IsChanPrefix(*mask))
  {
    /* List all users on a given channel */
    if ((chptr = hash_find_channel(mask)) != NULL)
    {
      if (IsMember(source_p, chptr))
        do_who_on_channel(source_p, chptr, chptr->chname, YES, server_oper);
      else if (!SecretChannel(chptr))
        do_who_on_channel(source_p, chptr, chptr->chname, NO, server_oper);
    }

    sendto_one(source_p, form_str(RPL_ENDOFWHO),
               from, to, mask);
    return;
  }

  /* '/who nick' */
  if (((target_p = find_client(mask)) != NULL) &&
      IsPerson(target_p) && (!server_oper || IsOper(target_p)))
  {
    int isinvis = 0;
 
    if (IsServer(client_p))
      client_burst_if_needed(client_p,target_p);

    isinvis = IsInvisible(target_p);
    DLINK_FOREACH_SAFE(lp, lp_next, target_p->user->channel.head)
    {
      chptr = ((struct Membership *) lp->data)->chptr;
      member = IsMember(source_p, chptr);
      if (isinvis && !member)
        continue;
      if (member || (!isinvis && PubChannel(chptr)))
        break;
    }

    if (lp != NULL)
      do_who(source_p, target_p, ((struct Membership *) lp->data)->chptr->chname,
             get_member_status(lp->data, NO));
    else
      do_who(source_p, target_p, NULL, "");

    sendto_one(source_p, form_str(RPL_ENDOFWHO),
               from, to, mask);
    return;
  }

  /* '/who 0' */
  if ((*(mask + 1) == '\0') && (*mask == '0'))
    who_global(source_p, NULL, server_oper);
  else
    who_global(source_p, mask, server_oper);

 /* Wasn't a nick, wasn't a channel, wasn't a '*' so ... */
  sendto_one(source_p, form_str(RPL_ENDOFWHO),
             from, to, mask);
}

/* who_common_channel
 * inputs	- pointer to client requesting who
 * 		- pointer to channel member chain.
 *		- char * mask to match
 *		- int if oper on a server or not
 *		- pointer to int maxmatches
 * output	- NONE
 * side effects - lists matching clients on specified channel,
 * 		  marks matched clients.
 *
 */
static void
who_common_channel(struct Client *source_p, struct Channel *chptr,
                   char *mask, int server_oper, int *maxmatches)
{
  dlink_node *ptr;
  dlink_node *ptr_next;
  struct Client *target_p;

  DLINK_FOREACH_SAFE(ptr, ptr_next, chptr->members.head)
  {
    target_p = ((struct Membership *)ptr->data)->client_p;

    if (!IsInvisible(target_p) || IsMarked(target_p))
      continue;

    if (server_oper && !IsOper(target_p))
      continue;

    SetMark(target_p);

    assert(target_p->user != NULL);
    assert(target_p->user->server != NULL);

    if ((mask == NULL) ||
	match(mask, target_p->name) || match(mask, target_p->username) ||
	match(mask, GET_CLIENT_HOST(target_p)) || 
	(match(mask, target_p->user->server->name)) ||
	match(mask, target_p->info))
    {
      do_who(source_p, target_p, NULL, "");

      if (*maxmatches > 0)
      {
	if (--(*maxmatches) == 0)
	  return;
      }
    }
  }
}

/* who_global()
 *
 * inputs	- pointer to client requesting who
 *		- char * mask to match
 *		- int if oper on a server or not
 * output	- NONE
 * side effects - do a global scan of all clients looking for match
 *		  this is slightly expensive on EFnet ...
 */
static void
who_global(struct Client *source_p,char *mask, int server_oper)
{
  struct Channel *chptr;
  struct Client *target_p;
  dlink_node *lp;
  dlink_node *lp_next;
  dlink_node *gcptr;
  dlink_node *gcptr_next;
  int maxmatches = 500;

  /* first, list all matching invisible clients on common channels */
  DLINK_FOREACH_SAFE(lp, lp_next, source_p->user->channel.head)
  {
    chptr = ((struct Membership *)lp->data)->chptr;
    who_common_channel(source_p, chptr, mask, server_oper, &maxmatches);
  }

  /* second, list all matching visible clients */
  DLINK_FOREACH_SAFE(gcptr, gcptr_next, global_client_list.head)
  {
    target_p = gcptr->data;

    if (!IsPerson(target_p))
      continue;

    if (IsInvisible(target_p))
    {
      ClearMark(target_p);
      continue;
    }

    if (server_oper && !IsOper(target_p))
      continue;

    assert(target_p->user->server != NULL);

    if (!mask ||
        match(mask, target_p->name) || match(mask, target_p->username) ||
	match(mask, GET_CLIENT_HOST(target_p)) || match(mask, target_p->user->server->name) ||
	match(mask, target_p->info))
    {
      do_who(source_p, target_p, NULL, "");

      if (maxmatches > 0)
      {
        if (--maxmatches == 0)
  	  return;
      }
    }
  }
}

/* do_who_on_channel()
 *
 * inputs	- pointer to client requesting who
 *		- pointer to channel to do who on
 *		- The "real name" of this channel
 *		- int if source_p is a server oper or not
 *		- int if client is member or not
 *		- int server_op flag
 * output	- NONE
 * side effects - do a who on given channel
 */
static void
do_who_on_channel(struct Client *source_p, struct Channel *chptr,
                  const char *chname, int member, int server_oper)
{
  dlink_node *ptr;
  dlink_node *ptr_next;
  struct Client *target_p;
  struct Membership *ms;

  DLINK_FOREACH_SAFE(ptr, ptr_next, chptr->members.head)
  {
    ms = ptr->data;
    target_p = ms->client_p;

    if (member || !IsInvisible(target_p))
    {
      if (server_oper && !IsOper(target_p))
	continue;
      do_who(source_p, target_p, chname, get_member_status(ms, NO));
    }
  }
}

/* do_who()
 *
 * inputs	- pointer to client requesting who
 *		- pointer to client to do who on
 *		- The reported name
 *		- channel flags
 * output	- NONE
 * side effects - do a who on given person
 */
static void
do_who(struct Client *source_p, struct Client *target_p,
       const char *chname, const char *op_flags)
{
  char status[5];
  const char *from, *to;

  if (!MyConnect(source_p) && IsCapable(source_p->from, CAP_TS6) && HasID(source_p))
  {
    from = me.id;
    to = source_p->id;
  }
  else
  {
    from = me.name;
    to = source_p->name;
  }

  ircsprintf(status, "%c%s%s", target_p->user->away ? 'G' : 'H',
             IsOper(target_p) ? "*" : "", op_flags);

  sendto_one(source_p, form_str(RPL_WHOREPLY), from, to,
	       (chname) ? (chname) : "*",
	       target_p->username,
	       GET_CLIENT_HOST(target_p),  target_p->user->server->name, target_p->name,
	       status, target_p->hopcount, target_p->info);
}

/*
** ms_who()
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
static void
ms_who(struct Client *client_p, struct Client *source_p,
       int parc, char *parv[])
{
  /* If its running as a hub, and linked with lazy links
   * then allow leaf to use normal client m_who()
   * other wise, ignore it.
   */
  if (ServerInfo.hub)
  {
    if (!IsCapable(client_p->from, CAP_LL))
      return;
  }

  m_who(client_p, source_p, parc, parv);
}
