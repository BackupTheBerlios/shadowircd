/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_whois.c: Shows who a user was.
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
 *  $Id: m_whowas.c,v 1.2 2004/01/20 19:56:34 nenolod Exp $
 */

#include "stdinc.h"
#include "whowas.h"
#include "handlers.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"


static void m_whowas(struct Client *, struct Client *, int, char **);
static void mo_whowas(struct Client *, struct Client *, int, char **);
static void whowas_do(struct Client *client_p, struct Client *source_p,
                      int parc, char *parv[]);

struct Message whowas_msgtab = {
  "WHOWAS", 0, 0, 0, 0, MFLG_SLOW, 0L,
  { m_unregistered, m_whowas, m_error, mo_whowas, m_ignore }
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&whowas_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&whowas_msgtab);
}

const char *_version = "$Revision: 1.2 $";
#endif

/*
** m_whowas
**      parv[0] = sender prefix
**      parv[1] = nickname queried
*/
static void
m_whowas(struct Client *client_p, struct Client *source_p,
         int parc, char *parv[])
{
  static time_t last_used = 0;

  if (parc < 2 || parv[1][0] == '\0')
  {
    sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN),
               me.name, source_p->name);
    return;
  }

  if ((last_used + ConfigFileEntry.pace_wait_simple) > CurrentTime)
  {
    sendto_one(source_p,form_str(RPL_LOAD2HI),
               me.name, source_p->name);
    return;
  }
  else
    last_used = CurrentTime;

  whowas_do(client_p, source_p, parc, parv);
}

static void
mo_whowas(struct Client *client_p, struct Client *source_p,
          int parc, char *parv[])
{
  if (parc < 2 || parv[1][0] == '\0')
  {
    sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN),
               me.name, source_p->name);
    return;
  }

  whowas_do(client_p, source_p, parc, parv);
}

static void
whowas_do(struct Client *client_p, struct Client *source_p,
          int parc, char *parv[])
{
  struct Whowas *temp;
  int cur = 0;
  int max = -1;
  int found = 0;
  char *p, *nick;

  if (parc > 2)
    max = atoi(parv[2]);
  if (parc > 3)
    if (hunt_server(client_p, source_p, ":%s WHOWAS %s %s :%s", 3, parc, parv))
      return;

  nick = parv[1];
  while (*nick == ',')
    nick++;
  if ((p = strchr(nick,',')) != NULL)
    *p = '\0';
  if (!*nick)
    return;

  temp  = WHOWASHASH[hash_whowas_name(nick)];

  for (; temp; temp = temp->next)
  {
    if (irccmp(nick, temp->name) == 0)
    {
      sendto_one(source_p, form_str(RPL_WHOWASUSER),
                 me.name, source_p->name, temp->name,
                 temp->username, temp->hostname,
                 temp->realname);

      sendto_one(source_p, form_str(RPL_WHOISSERVER),
                   me.name, source_p->name, temp->name,
                   temp->servername, myctime(temp->logoff));
      cur++;
      found++;
    }

    if (max > 0 && cur >= max)
      break;
  }

  if (!found)
    sendto_one(source_p, form_str(ERR_WASNOSUCHNICK),
               me.name, source_p->name, nick);

  sendto_one(source_p, form_str(RPL_ENDOFWHOWAS),
             me.name, source_p->name, parv[1]);
}
