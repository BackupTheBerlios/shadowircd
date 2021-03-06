/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_topic.c: Sets a channel topic.
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
 *  $Id: m_topic.c,v 1.4 2004/08/24 03:57:47 nenolod Exp $
 */

#include "stdinc.h"
#include "tools.h"
#include "handlers.h"
#include "channel.h"
#include "channel_mode.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"

static void m_topic (struct Client *, struct Client *, int, char **);
static void ms_topic (struct Client *, struct Client *, int, char **);

struct Message topic_msgtab = {
  "TOPIC", 0, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_topic, ms_topic, m_topic, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit (void)
{
  mod_add_cmd (&topic_msgtab);
}

void
_moddeinit (void)
{
  mod_del_cmd (&topic_msgtab);
}

const char *_version = "$Revision: 1.4 $";
#endif

/* m_topic()
 *  parv[0] = sender prefix
 *  parv[1] = channel name
 *  parv[2] = new topic, if setting topic
 */
static void
m_topic (struct Client *client_p, struct Client *source_p,
	 int parc, char *parv[])
{
  struct Channel *chptr = NULL;
  char *p;
  struct Membership *ms;
  const char *from, *to;

  if (!MyClient (source_p) && IsCapable (source_p->from, CAP_TS6)
      && HasID (source_p))
    {
      from = me.id;
      to = source_p->id;
    }
  else
    {
      from = me.name;
      to = source_p->name;
    }

  if ((p = strchr (parv[1], ',')) != NULL)
    *p = '\0';

  if (parv[1][0] == '\0')
    {
      sendto_one (source_p, form_str (ERR_NEEDMOREPARAMS), from, to, "TOPIC");
      return;
    }

  if (MyClient (source_p) && !IsFloodDone (source_p))
    flood_endgrace (source_p);

  if (IsChanPrefix (*parv[1]))
    {
      if ((chptr = hash_find_channel (parv[1])) == NULL)
	{
	  /* if chptr isn't found locally, it =could= exist
	   * on the uplink. so forward reqeuest
	   */
	  if (!ServerInfo.hub && uplink && IsCapable (uplink, CAP_LL))
	    {
	      sendto_one (uplink, ":%s TOPIC %s %s",
			  ID_or_name (source_p, uplink), parv[1],
			  ((parc > 2) ? parv[2] : ""));
	      return;
	    }
	  else
	    {
	      sendto_one (source_p, form_str (ERR_NOSUCHCHANNEL),
			  from, to, parv[1]);
	      return;
	    }
	}

      /* setting topic */
      if (parc > 2)
	{
	  if ((ms = find_channel_link (source_p, chptr)) == NULL)
	    {
	      if (MyClient (source_p))
		{
		  /* If it's local, stop it, otherwise let it go. */
		  sendto_one (source_p, form_str (ERR_NOTONCHANNEL), from,
			      to, parv[1]);
		  return;
		}
	    }

	  if (MyClient (source_p))
	    if (source_p->localClient->operflags & OPER_FLAG_OVERRIDE)
	      {
		char topic_info[USERHOST_REPLYLEN];
		ircsprintf (topic_info, "%s", source_p->name);
		set_channel_topic (chptr, parv[2], topic_info, CurrentTime);

		sendto_server (client_p, NULL, chptr, CAP_TS6, NOCAPS,
			       NOFLAGS, ":%s TOPIC %s :%s", ID (source_p),
			       chptr->chname,
			       chptr->topic == NULL ? "" : chptr->topic);
		sendto_server (client_p, NULL, chptr, NOCAPS, CAP_TS6,
			       NOFLAGS, ":%s TOPIC %s :%s", source_p->name,
			       chptr->chname,
			       chptr->topic == NULL ? "" : chptr->topic);
		sendto_channel_local (ALL_MEMBERS, chptr,
				      ":%s!%s@%s TOPIC %s :%s",
				      source_p->name, source_p->username,
				      GET_CLIENT_HOST (source_p),
				      chptr->chname,
				      chptr->topic ==
				      NULL ? "" : chptr->topic);
		return;
	      }

#ifndef DISABLE_CHAN_OWNER
	  if (((chptr->mode.mode & MODE_TOPICLOCK) &&
	       has_member_flags (ms, CHFL_CHANOWNER)) || !MyClient (source_p))
	    {
	      char topic_info[USERHOST_REPLYLEN];
	      ircsprintf (topic_info, "%s", source_p->name);
	      set_channel_topic (chptr, parv[2], topic_info, CurrentTime);

	      sendto_server (client_p, NULL, chptr, CAP_TS6, NOCAPS, NOFLAGS,
			     ":%s TOPIC %s :%s",
			     ID (source_p), chptr->chname,
			     chptr->topic == NULL ? "" : chptr->topic);
	      sendto_server (client_p, NULL, chptr, NOCAPS, CAP_TS6, NOFLAGS,
			     ":%s TOPIC %s :%s",
			     source_p->name, chptr->chname,
			     chptr->topic == NULL ? "" : chptr->topic);
	      sendto_channel_local (ALL_MEMBERS,
				    chptr, ":%s!%s@%s TOPIC %s :%s",
				    source_p->name,
				    source_p->username,
				    GET_CLIENT_HOST (source_p),
				    chptr->chname, chptr->topic == NULL ?
				    "" : chptr->topic);
	    }
	  else
#endif
            if (((chptr->mode.mode & MODE_TOPICLIMIT) &&
		    has_member_flags (ms,
				      CHFL_CHANOP | CHFL_HALFOP |
				      CHFL_CHANOWNER))
		   || !MyClient (source_p))
	    {
	      char topic_info[USERHOST_REPLYLEN];
	      ircsprintf (topic_info, "%s", source_p->name);
	      set_channel_topic (chptr, parv[2], topic_info, CurrentTime);

	      sendto_server (client_p, NULL, chptr, CAP_TS6, NOCAPS, NOFLAGS,
			     ":%s TOPIC %s :%s",
			     ID (source_p), chptr->chname,
			     chptr->topic == NULL ? "" : chptr->topic);
	      sendto_server (client_p, NULL, chptr, NOCAPS, CAP_TS6, NOFLAGS,
			     ":%s TOPIC %s :%s",
			     source_p->name, chptr->chname,
			     chptr->topic == NULL ? "" : chptr->topic);
	      sendto_channel_local (ALL_MEMBERS,
				    chptr, ":%s!%s@%s TOPIC %s :%s",
				    source_p->name,
				    source_p->username,
				    GET_CLIENT_HOST (source_p),
				    chptr->chname, chptr->topic == NULL ?
				    "" : chptr->topic);
	    }
	  else if (!(chptr->mode.mode & MODE_TOPICLIMIT))
	    {
	      char topic_info[USERHOST_REPLYLEN];
	      ircsprintf (topic_info, "%s", source_p->name);
	      set_channel_topic (chptr, parv[2], topic_info, CurrentTime);

	      sendto_server (client_p, NULL, chptr, CAP_TS6, NOCAPS, NOFLAGS,
			     ":%s TOPIC %s :%s",
			     ID (source_p), chptr->chname,
			     chptr->topic == NULL ? "" : chptr->topic);
	      sendto_server (client_p, NULL, chptr, NOCAPS, CAP_TS6, NOFLAGS,
			     ":%s TOPIC %s :%s",
			     source_p->name, chptr->chname,
			     chptr->topic == NULL ? "" : chptr->topic);
	      sendto_channel_local (ALL_MEMBERS,
				    chptr, ":%s!%s@%s TOPIC %s :%s",
				    source_p->name,
				    source_p->username,
				    GET_CLIENT_HOST (source_p),
				    chptr->chname, chptr->topic == NULL ?
				    "" : chptr->topic);
	    }
	  else
	    sendto_one (source_p, form_str (ERR_CHANOPRIVSNEEDED),
			from, to, parv[1]);
	}
      else			/* only asking for topic */
	{
	  if (!SecretChannel (chptr) || IsMember (source_p, chptr))
	    {
	      if (chptr->topic == NULL)
		sendto_one (source_p, form_str (RPL_NOTOPIC),
			    from, to, parv[1]);
	      else
		{
		  sendto_one (source_p, form_str (RPL_TOPIC),
			      from, to, chptr->chname, chptr->topic);

		  sendto_one (source_p, form_str (RPL_TOPICWHOTIME),
			      from, to, chptr->chname,
			      chptr->topic_info, chptr->topic_time);
		}
	    }
	  else
	    {
	      sendto_one (source_p, form_str (ERR_NOTONCHANNEL),
			  from, to, parv[1]);
	      return;
	    }
	}
    }
  else
    {
      sendto_one (source_p, form_str (ERR_NOSUCHCHANNEL), from, to, parv[1]);
    }
}

/*
 * ms_topic
 *      parv[0] = sender prefix
 *      parv[1] = channel name
 *	parv[2] = topic_info
 *	parv[3] = topic_info time
 *	parv[4] = new channel topic
 *
 * Let servers always set a topic
 */
static void
ms_topic (struct Client *client_p, struct Client *source_p,
	  int parc, char *parv[])
{
  struct Channel *chptr = NULL;

  if (!IsServer (source_p))
    {
      m_topic (client_p, source_p, parc, parv);
      return;
    }

  if (parc < 5)
    return;

  if (parv[1] && IsChanPrefix (*parv[1]))
    {
      if ((chptr = hash_find_channel (parv[1])) == NULL)
	return;

      set_channel_topic (chptr, parv[4], parv[2], atoi (parv[3]));

      sendto_channel_local (ALL_MEMBERS,
			    chptr, ":%s TOPIC %s :%s",
			    source_p->name,
			    parv[1],
			    chptr->topic == NULL ? "" : chptr->topic);
    }
}
