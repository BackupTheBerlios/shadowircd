/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_locops.c: Sends a message to all operators on the local server.
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
 *  $Id: m_locops.c,v 1.2 2004/02/09 23:02:24 nenolod Exp $
 */

#include "stdinc.h"
#include "handlers.h"
#include "client.h"
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_user.h"
#include "s_serv.h"
#include "hash.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "cluster.h"

static void m_locops (struct Client *, struct Client *, int, char **);
static void ms_locops (struct Client *, struct Client *, int, char **);

struct Message locops_msgtab = {
  "LOCOPS", 0, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_locops, m_locops, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit (void)
{
  mod_add_cmd (&locops_msgtab);
}

void
_moddeinit (void)
{
  mod_del_cmd (&locops_msgtab);
}

const char *_version = "$Revision: 1.2 $";
#endif

/*
 * m_locops - LOCOPS message handler
 * (write to *all* local opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = message text
 */
static void
m_locops (struct Client *client_p, struct Client *source_p,
	  int parc, char *parv[])
{
  const char *message = parv[1];

  if (EmptyString (message))
    {
      sendto_one (source_p, form_str (ERR_NEEDMOREPARAMS),
		  me.name, source_p->name, "LOCOPS");
      return;
    }

  sendto_wallops_flags (UMODE_LOCOPS, source_p, "LOCOPS - %s", message);

  if (dlink_list_length (&cluster_items))
    cluster_locops (source_p, parv[1]);
}

static void
ms_locops (struct Client *client_p, struct Client *source_p,
	   int parc, char *parv[])
{
  if (parc != 3 || EmptyString (parv[2]))
    return;

  sendto_server (client_p, NULL, NULL, CAP_CLUSTER, 0, 0, "LOCOPS %s :%s",
		 parv[1], parv[2]);

  if (!match (parv[1], me.name))
    return;

  if (!IsPerson (source_p))
    return;

  sendto_wallops_flags (UMODE_LOCOPS, source_p, "LOCOPS - %s", parv[2]);
}
