/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_kill.c: Kills a user.
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
 *  $Id: m_kill.c,v 1.3 2004/01/20 19:56:34 nenolod Exp $
 */

#include "stdinc.h"
#include "handlers.h"
#include "client.h"
#include "hash.h"       /* for find_client() */
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_conf.h"
#include "send.h"
#include "whowas.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"


static char buf[BUFSIZE];

static void ms_kill(struct Client *, struct Client *, int, char **);
static void mo_kill(struct Client *, struct Client *, int, char **);
static void relay_kill(struct Client *, struct Client *, struct Client *,
                       const char *, const char *);

struct Message kill_msgtab = {
  "KILL", 0, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_kill, mo_kill, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&kill_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&kill_msgtab);
}

const char *_version = "$Revision: 1.3 $";
#endif

/* mo_kill()
 *  parv[0] = sender prefix
 *  parv[1] = kill victim
 *  parv[2] = kill path
 */
static void
mo_kill(struct Client *client_p, struct Client *source_p,
        int parc, char *parv[])
{
  struct Client *target_p;
  const char *inpath = client_p->name;
  char *user;
  char *reason;
  char def_reason[] = "No reason specified";

  user   = parv[1];
  reason = parv[2]; /* Either defined or NULL (parc >= 2!!) */

  if (*user == '\0')
  {
    sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
               me.name, source_p->name, "KILL");
    return;
  }

  if (!IsOperK(source_p) && !IsOperGlobalKill(source_p))
  {
    sendto_one(source_p, form_str(ERR_NOPRIVILEGES),
               me.name, source_p->name);
    return;
  }

  if (!EmptyString(reason))
  {
    if (strlen(reason) > (size_t)KILLLEN)
      reason[KILLLEN] = '\0';
  }
  else
    reason = def_reason;

  if ((target_p = find_client(user)) == NULL)
  {
    /* If the user has recently changed nick, automatically
     * rewrite the KILL for this new nickname--this keeps
     * servers in synch when nick change and kill collide
     */
    if ((target_p = get_history(user, 
				(time_t)ConfigFileEntry.kill_chase_time_limit))
				== NULL)
    {
      sendto_one(source_p, form_str(ERR_NOSUCHNICK),
                 me.name, source_p->name, user);
      return;
    }

    sendto_one(source_p, ":%s NOTICE %s :KILL changed from %s to %s",
               me.name, source_p->name, user, target_p->name);
  }

  if (IsServer(target_p) || IsMe(target_p))
  {
    sendto_one(source_p, form_str(ERR_CANTKILLSERVER),
               me.name, source_p->name);
    return;
  }

  if (!MyConnect(target_p) && (!IsOperGlobalKill(source_p)))
  {
    sendto_one(source_p, ":%s NOTICE %s :Nick %s isnt on your server",
               me.name, source_p->name, target_p->name);
    return;
  }

  if (MyConnect(target_p))
    sendto_one(target_p, ":%s!%s@%s KILL %s :%s", 
               source_p->name, source_p->username, GET_CLIENT_HOST(source_p),
               target_p->name, reason);

  /* Do not change the format of this message.  There's no point in changing messages
   * that have been around for ever, for no reason.. */
  sendto_realops_flags(UMODE_ALL, L_ALL,
		       "Received KILL message for %s. From %s Path: %s (%s)", 
		       target_p->name, source_p->name, me.name, reason);

  ilog(L_INFO, "KILL From %s For %s Path %s (%s)",
       source_p->name, target_p->name, me.name, reason);

  /*
  ** And pass on the message to other servers. Note, that if KILL
  ** was changed, the message has to be sent to all links, also
  ** back.
  ** Suicide kills are NOT passed on --SRB
  */
  if (!MyConnect(target_p))
  {
    relay_kill(client_p, source_p, target_p, inpath, reason);
      /*
      ** Set FLAGS_KILLED. This prevents exit_one_client from sending
      ** the unnecessary QUIT for this. (This flag should never be
      ** set in any other place)
      */
    SetKilled(target_p);
  }

  ircsprintf(buf, "Killed (%s (%s))", source_p->name, reason);
  exit_client(client_p, target_p, source_p, buf);
}

/* ms_kill()
 *  parv[0] = sender prefix
 *  parv[1] = kill victim
 *  parv[2] = kill path and reason
 */
static void
ms_kill(struct Client *client_p, struct Client *source_p,
        int parc, char *parv[])
{
  struct Client *target_p;
  char *user;
  char *reason;
  const char *path;
  char def_reason[] = "No reason specified";

  *buf = '\0';

  if (*parv[1] == '\0')
  {
    sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
               me.name, source_p->name, "KILL");
    return;
  }

  user = parv[1];

  if (EmptyString(parv[2]))
  {
    reason = def_reason;

    /* hyb6 takes the nick of the killer from the path *sigh* --fl_ */
    path = source_p->name;
  }
  else
  {
    reason = strchr(parv[2], ' ');

    if (reason != NULL)
    {
      *reason = '\0';
      reason++;
    }
    else
      reason = def_reason;

    path = parv[2];
  }

  if ((target_p = find_person(user)) == NULL)
  {
      /* If the user has recently changed nick, but only if its 
       * not an uid, automatically rewrite the KILL for this new nickname.
       * --this keeps servers in synch when nick change and kill collide
       */
      if (*user == '.' ||
	  (target_p = get_history(user,
                                (time_t)ConfigFileEntry.kill_chase_time_limit))
                                == NULL)
      {
        sendto_one(source_p, form_str(ERR_NOSUCHNICK),
                   me.name, source_p->name, user);
        return;
      }
      sendto_one(source_p,":%s NOTICE %s :KILL changed from %s to %s",
                 me.name, source_p->name, user, target_p->name);
  }

  if (IsServer(target_p) || IsMe(target_p))
  {
    sendto_one(source_p, form_str(ERR_CANTKILLSERVER),
               me.name, source_p->name);
    return;
  }

  if (MyConnect(target_p))
  {
    if (IsServer(source_p))
    {
      sendto_one(target_p, ":%s KILL %s :%s",
	         source_p->name, target_p->name, reason);
    }
    else
      sendto_one(target_p, ":%s!%s@%s KILL %s :%s",
		 source_p->name, source_p->username, GET_CLIENT_HOST(source_p),
		 target_p->name, reason);
  }

  /* Be warned, this message must be From %s, or it confuses clients
   * so dont change it to From: or the case or anything! -- fl -- db */
  /* path must contain at least 2 !'s, or bitchx falsely declares it
   * local --fl
   */
  if (IsOper(source_p)) /* send it normally */
  {
    sendto_realops_flags(UMODE_ALL, L_ALL,
                         "Received KILL message for %s. From %s Path: %s!%s!%s!%s %s",
                         target_p->name, source_p->name, source_p->user->server->name, 
                         GET_CLIENT_HOST(source_p), source_p->username, source_p->name, reason);
  }
  else
  {
    sendto_realops_flags(UMODE_SKILL, L_ALL,
                         "Received KILL message for %s. From %s %s",
                         target_p->name, source_p->name, reason);
  }

  ilog(L_INFO,"KILL From %s For %s Path %s %s",
       source_p->name, target_p->name, source_p->name, reason);

  relay_kill(client_p, source_p, target_p, path, reason);
  SetKilled(target_p);

  ircsprintf(buf, "Killed (%s %s)", source_p->name, reason);

  exit_client(client_p, target_p, source_p, buf);
}

static void
relay_kill(struct Client *one, struct Client *source_p,
           struct Client *target_p, const char *inpath, const char *reason)
{
  dlink_node *ptr;
  struct Client *client_p;
  int introduce_killed_client;
  const char *from, *to;

  /* LazyLinks:
   * Check if each lazylink knows about target_p.
   *   If it does, send the kill, introducing source_p if required.
   *   If it doesn't either:
   *     a) don't send the kill (risk ghosts)
   *     b) introduce the client (and source_p, if required)
   *        [rather redundant]
   *
   * Use a) if IsServer(source_p), but if an oper kills someone,
   * ensure we blow away any ghosts.
   *
   * -davidt
   */

  if (IsServer(source_p))
    introduce_killed_client = 0;
  else
    introduce_killed_client = 1;

  DLINK_FOREACH(ptr, serv_list.head)
  {
    client_p = ptr->data;

    if (client_p == NULL || client_p == one)
      continue;

    if (!introduce_killed_client)
    {
      if (ServerInfo.hub && IsCapable(client_p, CAP_LL))
      {
        if ((client_p->localClient->serverMask &
             target_p->lazyLinkClientExists) == 0)
        {
          /* target isn't known to lazy leaf, skip it */
          continue;
        }
      }
    }
    /* force introduction of killed client but check that
     * its not on the server we're bursting too.. */
    else if (strcmp(target_p->user->server->name, client_p->name))
      client_burst_if_needed(client_p, target_p);

    /* introduce source of kill */
    client_burst_if_needed(client_p, source_p);

    /* use UID if possible */
    from = ID_or_name(source_p, client_p);
    to = ID_or_name(target_p, client_p);

    if (MyClient(source_p))
    {
        sendto_one(client_p, ":%s KILL %s :%s!%s!%s!%s (%s)",
                   from, to,
                   me.name, source_p->host, source_p->username,
                   source_p->name, reason);
    }
    else
    {
        sendto_one(client_p, ":%s KILL %s :%s %s",
                   from, to,
                   inpath, reason);
    }
  }
}

