/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_setname.c: Resets a user's gecos information.
 *
 *  Copyright (C) 2004 by the past and present ircd coders, and others.
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
 *  $Id: m_setname.c,v 1.3 2004/06/10 21:48:50 nenolod Exp $
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
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "whowas.h"		/* off_history */
#include "s_user.h"

void m_setname (struct Client *client_p, struct Client *source_p, int parc,
		 char *parv[]);

struct Message setname_msgtab = {
  "SETNAME", 0, 0, 1, 0, MFLG_SLOW, 0,
  {m_unregistered, m_setname, m_setname, m_setname}
};

#ifndef STATIC_MODULES
void
_modinit (void)
{
  mod_add_cmd (&setname_msgtab);
}

void
_moddeinit (void)
{
  mod_del_cmd (&setname_msgtab);
}

const char *_version = "$Revision: 1.3 $";
#endif

/* m_svscloak
 * parv[1] - Nick to cloak
 * parv[2] - Hostname to cloak to
 */

void
m_setname (struct Client *client_p, struct Client *source_p, int parc,
	    char *parv[])
{
  struct Client *target_p;
  char *hostname, *target;

  if (parc < 3 || EmptyString (parv[2]))
    {
      sendto_one (source_p, form_str (ERR_NEEDMOREPARAMS), me.name, parv[0], "SETCLOAK");
      return;
    }
  target = parv[1];
  hostname = parv[2];

  if ((target_p = find_person (target)))
    {
      if (MyConnect (source_p))
	{
	  if (source_p == target_p)
	    {
	      if (!IsOperSetOwnCloak(source_p))
		{
		  return;
		}
	    }
	  else
	    {
	      if (!IsOperSetAnyCloak(source_p))
		{
		  return;
		}
	    }
	}
      sendto_server (client_p, NULL, NULL, NOCAPS, NOCAPS, NOFLAGS,
		     ":%s SVSNAME %s :%s", parv[0], parv[1], parv[2]);
      strncpy (target_p->info, hostname, USERLEN);
    }
  else
    {
      sendto_one (source_p, form_str (ERR_NOSUCHNICK), me.name,
		  source_p->name, target);
      return;
    }
  return;
}
