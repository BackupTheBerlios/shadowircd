/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_whois.c: Shows who a user is.
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
 *  $Id: m_whois.c,v 1.11 2004/04/01 20:07:14 nenolod Exp $
 */

#include "stdinc.h"
#include "tools.h"
#include "common.h"
#include "handlers.h"
#include "client.h"
#include "hash.h"
#include "channel.h"
#include "channel_mode.h"
#include "hash.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "hook.h"

/* Add the bahamut style WHOISACTUALLY to show the users real ip on
   remote (or local) WHOIS.
   Added 2002-07-15 by Gozem
   Updated 2003-01-17 by MeTaLRoCk
*/

static void do_whois (struct Client *client_p, struct Client *source_p,
		      int parc, char *parv[]);
static int single_whois (struct Client *source_p, struct Client *target_p,
			 int wilds, int glob);
static void whois_person (struct Client *source_p, struct Client *target_p,
			  int glob);
static int global_whois (struct Client *source_p, char *nick, int wilds,
			 int glob);

static void m_whois (struct Client *, struct Client *, int, char **);
static void ms_whois (struct Client *, struct Client *, int, char **);
static void mo_whois (struct Client *, struct Client *, int, char **);


struct Message whois_msgtab = {
  "WHOIS", 0, 0, 0, 0, MFLG_SLOW, 0L,
  {m_unregistered, m_whois, ms_whois, mo_whois, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit (void)
{
  hook_add_event ("doing_whois");
  mod_add_cmd (&whois_msgtab);
}

void
_moddeinit (void)
{
  hook_del_event ("doing_whois");
  mod_del_cmd (&whois_msgtab);
}

const char *_version = "$Revision: 1.11 $";
#endif

/* m_whois
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
static void
m_whois (struct Client *client_p, struct Client *source_p,
	 int parc, char *parv[])
{
  static time_t last_used = 0;

  if (parc < 2 || EmptyString (parv[1]))
    {
      sendto_one (source_p, form_str (ERR_NONICKNAMEGIVEN),
		  me.name, source_p->name);
      return;
    }

  if (parc > 2)
    {
      /* seeing as this is going across servers, we should limit it */
      if ((last_used + ConfigFileEntry.pace_wait_simple) > CurrentTime)
	{
	  if (MyClient (source_p))
	    sendto_one (source_p, form_str (RPL_LOAD2HI),
			me.name, source_p->name);
	  return;
	}
      else
	last_used = CurrentTime;

      if (hunt_server (client_p, source_p, ":%s WHOIS %s :%s", 1, parc, parv)
	  != HUNTED_ISME)
	return;

    }
  do_whois (client_p, source_p, parc, parv);
}

/* mo_whois
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
static void
mo_whois (struct Client *client_p, struct Client *source_p,
	  int parc, char *parv[])
{
  if (parc < 2 || EmptyString (parv[1]))
    {
      sendto_one (source_p, form_str (ERR_NONICKNAMEGIVEN),
		  me.name, source_p->name);
      return;
    }

  if (parc > 2)
    {
      if (hunt_server (client_p, source_p, ":%s WHOIS %s :%s", 1, parc, parv)
	  != HUNTED_ISME)
	return;
      parv[1] = parv[2];
    }
  do_whois (client_p, source_p, parc, parv);
}

/* do_whois()
 *
 * inputs	- pointer to 
 * output	- 
 * side effects -
 */
static void
do_whois (struct Client *client_p, struct Client *source_p,
	  int parc, char *parv[])
{
  struct Client *target_p;
  char *nick;
  char *p = NULL;
  int found = NO;
  int wilds;
  int glob = 0;

  /* This lets us make all "whois nick" queries look the same, and all
   * "whois nick nick" queries look the same.  We have to pass it all
   * the way down to whois_person() though -- fl */

  if (parc > 2)
    glob = 1;

  nick = parv[1];
  while (*nick == ',')
    nick++;

  if ((p = strchr (nick, ',')) != NULL)
    *p = '\0';

  if (*nick == '\0')
    return;

  collapse (nick);
  wilds = (strchr (nick, '?') || strchr (nick, '*'));

  if (!wilds)
    {
      if ((target_p = find_client (nick)) != NULL)
	{
	  /* im being asked to reply to a client that isnt mine..
	   * I cant answer authoritively, so better make it non-detailed
	   */
	  if (!MyClient (target_p))
	    glob = 0;

	  if (IsServer (client_p))
	    client_burst_if_needed (client_p, target_p);

	  if (IsPerson (target_p))
	    {
	      (void) single_whois (source_p, target_p, wilds, glob);
	      found = YES;
	    }
	}
      else
	{
	  if (!ServerInfo.hub && uplink && IsCapable (uplink, CAP_LL))
	    {
	      if (glob)
		sendto_one (uplink, ":%s WHOIS %s :%s",
			    source_p->name, nick, nick);
	      else
		sendto_one (uplink, ":%s WHOIS %s", source_p->name, nick);
	      return;
	    }
	}
    }
  else				/* wilds is true */
    {
      /* disallow wild card whois on lazylink leafs for now */
      if (!ServerInfo.hub && uplink && IsCapable (uplink, CAP_LL))
	return;
      /* Oh-oh wilds is true so have to do it the hard expensive way */
      if (MyClient (source_p))
	found = global_whois (source_p, nick, wilds, glob);
    }
  if (!found)
    sendto_one (source_p, form_str (ERR_NOSUCHNICK),
		me.name, source_p->name, nick);

  sendto_one (source_p, form_str (RPL_ENDOFWHOIS),
	      me.name, source_p->name, parv[1]);
}

/* global_whois()
 *
 * Inputs	- source_p client to report to
 *		- target_p client to report on
 *		- wilds whether wildchar char or not
 * Output	- if found return 1
 * Side Effects	- do a single whois on given client
 * 		  writing results to source_p
 */
static int
global_whois (struct Client *source_p, char *nick, int wilds, int glob)
{
  dlink_node *ptr;
  struct Client *target_p;
  int found = 0;

  DLINK_FOREACH (ptr, global_client_list.head)
  {
    target_p = ptr->data;

    if (!IsPerson (target_p))
      continue;

    if (!match (nick, target_p->name))
      continue;

    assert (target_p->user->server != NULL);

    /* 'Rules' established for sending a WHOIS reply:
     *
     *
     * - if wildcards are being used dont send a reply if
     *   the querier isnt any common channels and the
     *   client in question is invisible and wildcards are
     *   in use (allow exact matches only);
     *
     * - only send replies about common or public channels
     *   the target user(s) are on;
     */

    if (single_whois (source_p, target_p, wilds, glob))
      found = 1;

  }
  return (found);
}

/* single_whois()
 *
 * Inputs	- source_p client to report to
 *		- target_p client to report on
 *		- wilds whether wildchar char or not
 * Output	- if found return 1
 * Side Effects	- do a single whois on given client
 * 		  writing results to source_p
 */
static int
single_whois (struct Client *source_p, struct Client *target_p,
	      int wilds, int glob)
{
  dlink_node *ptr;
  struct Channel *chptr;
  const char *name;
  int invis;
  int member;
  int showperson;

  assert (target_p->user != NULL);

  if (target_p->name[0] == '\0')
    name = "?";
  else
    name = target_p->name;

  invis = IsInvisible (target_p);
  member = (target_p->user->channel.head != NULL) ? 1 : 0;
  showperson = (wilds && !invis && !member) || !wilds;

  DLINK_FOREACH (ptr, target_p->user->channel.head)
  {
    chptr = ((struct Membership *) ptr->data)->chptr;
    member = IsMember (source_p, chptr);
    if (invis && !member)
      continue;
    if (member || (!invis && PubChannel (chptr)))
      {
	showperson = 1;
	break;
      }
    if (!invis && HiddenChannel (chptr) && !SecretChannel (chptr))
      {
	showperson = 1;
	break;
      }
  }

  if (showperson)
    whois_person (source_p, target_p, glob);

  return (1);
}

/* whois_person()
 *
 * Inputs	- source_p client to report to
 *		- target_p client to report on
 * Output	- NONE
 * Side Effects	- 
 */
static void
whois_person (struct Client *source_p, struct Client *target_p, int glob)
{
  char buf[BUFSIZE];
  dlink_node *lp;
  struct Client *server_p;
  struct Channel *chptr;
  struct Membership *ms;
  int cur_len = 0;
  int mlen;
  char *t;
  int tlen;
  int reply_to_send = NO;
  struct hook_mfunc_data hd;

  assert (target_p->user != NULL);

  server_p = target_p->user->server;

  sendto_one (source_p, form_str (RPL_WHOISUSER), me.name,
	      source_p->name, target_p->name,
	      target_p->username, GET_CLIENT_HOST (target_p), target_p->info);

  if (target_p->umodes & UMODE_WHOIS)
    sendto_one (target_p, ":%s NOTICE %s :*** Notice -- %s (%s@%s) has /whois'ed you.",
                me.name, target_p->name, source_p->name, source_p->username,
                GET_CLIENT_HOST(source_p));

  ircsprintf (buf, form_str (RPL_WHOISCHANNELS),
	      me.name, source_p->name, target_p->name, "");

  mlen = strlen (buf);
  cur_len = mlen;
  t = buf + mlen;

  DLINK_FOREACH (lp, target_p->user->channel.head)
  {
    ms = lp->data;
    chptr = ms->chptr;

    if (ShowChannel (source_p, chptr))
      {
	if ((cur_len + strlen (chptr->chname) + 2) > (BUFSIZE - 4))
	  {
	    sendto_one (source_p, "%s", buf);
	    cur_len = mlen;
	    t = buf + mlen;
	  }

	ircsprintf (t, "%s%s ", get_member_status (ms, YES), chptr->chname);

	tlen = strlen (t);
	t += tlen;
	cur_len += tlen;
	reply_to_send = YES;
      }
  }

  if (reply_to_send)
    sendto_one (source_p, "%s", buf);

  if (!IsOperAuspex(source_p))
    sendto_one (source_p, form_str (RPL_WHOISSERVER), me.name, source_p->name,
		target_p->name, ServerInfo.whois_hiding, 
                ServerInfo.whois_description);
  else
    sendto_one (source_p, form_str (RPL_WHOISSERVER), me.name, source_p->name,
		target_p->name, server_p->name, server_p->info);

  if (target_p->user->away)
    sendto_one (source_p, form_str (RPL_AWAY), me.name,
		source_p->name, target_p->name, target_p->user->away);

  if (target_p->umodes & UMODE_IDENTIFY)
    sendto_one (source_p, form_str (RPL_HASIDENTIFIED),
		me.name, source_p->name, target_p->name);

  if (IsOper (target_p))
    {
      if (!(target_p->umodes & UMODE_HIDEOPER))
	{
          if (target_p->umodes & UMODE_NETADMIN)
            {
              sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
                          me.name, source_p->name, target_p->name,
                          "an IRC Operator - Network Administrator");
            }
          else if (target_p->umodes & UMODE_TECHADMIN)
            {
              sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
                          me.name, source_p->name, target_p->name,
                          "an IRC Operator - Technical Administrator");
            }
	  else if (target_p->umodes & UMODE_SERVICE)
	    {
	      sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
			  me.name, source_p->name, target_p->name,
			  "a Network Service");
	    }
	  else if (target_p->umodes & UMODE_SVSROOT)
	    {
	      sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
			  me.name, source_p->name, target_p->name,
			  "an IRC Operator - Services Root Administrator");
	    }
	  else if (target_p->umodes & UMODE_SVSADMIN)
	    {
	      sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
			  me.name, source_p->name, target_p->name,
			  "an IRC Operator - Services Administrator");
	    }
	  else if (target_p->umodes & UMODE_ADMIN)
	    {
	      sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
			  me.name, source_p->name, target_p->name,
			  "an IRC Operator - Server Administrator");
	    }
	  else if (target_p->umodes & UMODE_SVSOPER)
	    {
	      sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
			  me.name, source_p->name, target_p->name,
			  "an IRC Operator - Services Operator");
	    }
	  else if (target_p->umodes & UMODE_OPER)
	    {
	      sendto_one (source_p, form_str (RPL_WHOISOPERATOR),
			  me.name, source_p->name, target_p->name,
			  "an IRC Operator");
	    }
	}
    }

  if (target_p->umodes & UMODE_HELPOP)
    sendto_one (source_p, form_str (RPL_WHOISHELPOP),
		me.name, source_p->name, target_p->name);

  if (target_p->umodes & UMODE_SECURE)
    sendto_one (source_p, form_str (RPL_WHOISSECURE),
		me.name, source_p->name, target_p->name);

  if (MyConnect (target_p))	/* Can't do any of this if not local! db */
    {
      if ((glob) || (MyClient (source_p)))
	{
	  if (IsOperAuspex (source_p))
	    sendto_one (source_p, form_str (RPL_WHOISACTUALLY),
			me.name, source_p->name, target_p->name,
			target_p->localClient->sockhost);
	  sendto_one (source_p, form_str (RPL_WHOISIDLE),
		      me.name, source_p->name, target_p->name,
		      CurrentTime - target_p->user->last,
		      target_p->firsttime);
	}
    }

  hd.client_p = target_p;
  hd.source_p = source_p;
  /* although we should fill in parc and parv, we don't ..
   * be careful of this when writing whois hooks
   */
  hook_call_event ("doing_whois", &hd);
}

/* ms_whois()
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
/* Be warned, this is heavily commented, as theres loads of possibilities
 * that can happen, and as people might not understand the code, I
 * stuck heavy comments in it.. it looks ugly.. but at least you
 * know what it does.. --fl_ */
static void
ms_whois (struct Client *client_p,
	  struct Client *source_p, int parc, char *parv[])
{
  if (!IsClient (source_p))
    return;
  /* its a misconfigured server */
  if (parc < 2 || EmptyString (parv[1]))
    {
      sendto_one (source_p,
		  form_str (ERR_NONICKNAMEGIVEN), me.name, source_p->name);
      return;
    }

  /* its a client doing a remote whois:
   * :parv[0] WHOIS parv[1] :parv[2]
   *
   * parv[0] == sender
   * parv[1] == server to reply to the request
   * parv[2] == the client we are whois'ing
   */
  if (parc > 2)
    {
      struct Client *target_p;
      /* check if parv[1] is a person.. (most common) */
      if ((target_p = find_person (parv[1])) == NULL)
	{
	  /* ok, parv[1] isnt a client, is it a server? */
	  if ((target_p = find_server (parv[1])) == NULL)
	    {
	      /* its not a server either.. theyve sent a bad whois */
	      sendto_one (source_p, form_str (ERR_NOSUCHSERVER),
			  me.name, source_p->name, parv[1]);
	      return;
	    }
	}

      /* its been found as a client.. to save extra work later, change
       * target_p to be the clients uplink.. you cant check if a client
       * supports being a LL :P
       */
      else
	target_p = target_p->servptr;
      /* at this point, we know target_p is a server, so client
       * specific checks are a waste of time.  if target_p isnt me,
       * someone else is supposed to be handling the request.. so
       * send it to them
       */
      if (!IsMe (target_p))
	{

	  /* we're being asked to forward a whois request to a LL to answer,
	   * if the target isnt on that server.. answer it for them, as the
	   * LL might not know that the target exists..
	   */
	  if (MyConnect (target_p) && ServerInfo.hub &&
	      IsCapable (target_p, CAP_LL))
	    {
	      struct Client *whois_p;
	      /* try to find the client */
	      if ((whois_p = find_client (parv[2])) != NULL)
		{
		  /* check the server being asked to perform the whois, is that
		   * clients uplink */
		  if (whois_p->servptr != target_p)
		    {
		      /* they asked the LL for info on a client it didnt know about..
		       * as we're answering for them, make sure its non-detailed
		       */
		      parv[1] = parv[2];
		      parc = 2;
		      do_whois (client_p, source_p, parc, parv);
		      return;
		    }
		  /* the server is that clients uplink, get them to do it */
		  else
		    {
		      client_burst_if_needed (target_p->from, source_p);
		      sendto_one (target_p->from, ":%s WHOIS %s :%s",
				  parv[0], parv[1], parv[2]);
		      return;
		    }
		}
	      /* the client doesnt exist.. erk! */
	      else
		{
		  /* set parv[1] = parv[2], then let do_whois handle the error */
		  parv[1] = parv[2];
		  do_whois (client_p, source_p, parc, parv);
		  return;
		}
	    }
	  /* its not a lazylink.. forward it, but replace nicks/servernames with IDs where needed */
	  else
	    {
	      if (IsCapable (target_p->from, CAP_TS6)
		  && HasID (source_p) && HasID (target_p))
		sendto_one (target_p->from,
			    ":%s WHOIS %s :%s",
			    source_p->id, target_p->id, parv[2]);
	      else
		sendto_one (target_p->from, ":%s WHOIS %s :%s",
			    parv[0], parv[1], parv[2]);
	      return;
	    }
	}

      /* ok, the target is either us, or a client on our server, so perform the whois
       * but first, parv[1] == server to perform the whois on, parv[2] == person
       * to whois, so make parv[1] = parv[2] so do_whois is ok -- fl_
       */
      parv[1] = parv[2];
      do_whois (client_p, source_p, parc, parv);
      return;
    }

  do_whois (client_p, source_p, parc, parv);
}
