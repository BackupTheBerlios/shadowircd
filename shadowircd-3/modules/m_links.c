/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_links.c: Shows what servers are currently connected.
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
 *  $Id: m_links.c,v 3.3 2004/09/08 01:18:07 nenolod Exp $
 */

#include "stdinc.h"
#include "handlers.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"
#include "motd.h"
#include "parse.h"
#include "modules.h"
#include "hook.h"


static void m_links(struct Client*, struct Client*, int, char**);
static void mo_links(struct Client*, struct Client*, int, char**);
static void ms_links(struct Client*, struct Client*, int, char**);

struct Message links_msgtab = {
  "LINKS", 0, 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_links, ms_links, mo_links, m_ignore}
};
#ifndef STATIC_MODULES

void
_modinit(void)
{
	hook_add_event("doing_links");
  mod_add_cmd(&links_msgtab);
}

void
_moddeinit(void)
{
	hook_del_event("doing_links");
  mod_del_cmd(&links_msgtab);
}

const char *_version = "$Revision: 3.3 $";
#endif

/*
 * m_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */
static void
m_links(struct Client *client_p, struct Client *source_p,
        int parc, char *parv[])
{
  mo_links(client_p, source_p, parc, parv);
}

static void
mo_links(struct Client *client_p, struct Client *source_p,
         int parc, char *parv[])
{
  const char *mask = "";
  struct Client *target_p;
  char clean_mask[2 * HOSTLEN + 4];
  const char *p;
  struct hook_links_data hd;
  dlink_node *ptr;

  if (parc > 2) 
  {
    if (!ConfigFileEntry.disable_remote || IsOper(source_p))
    {
        if (hunt_server(client_p, source_p, ":%s LINKS %s :%s", 1, parc, parv)
            != HUNTED_ISME)
        return;
    }

    mask = parv[2];
  }
  else if (parc == 2)
    mask = parv[1];

  assert(0 != mask);

  if (*mask)       /* only necessary if there is a mask */
    mask = collapse(clean_string(clean_mask, (const unsigned char*) mask, 2 * HOSTLEN));

  hd.client_p = client_p;
  hd.source_p = source_p;
  hd.mask = mask;
  hd.parc = parc;
  hd.parv = parv;
  
  hook_call_event("doing_links", &hd);
  
  DLINK_FOREACH(ptr, global_serv_list.head)
    {
      target_p = ptr->data;

      if (*mask && !match(mask, target_p->name))
        continue;
    
      if(target_p->info[0])
        {
          if( (p = strchr(target_p->info,']')) )
            p += 2; /* skip the nasty [IP] part */
          else
            p = target_p->info;
        }
      else
        p = "(Unknown Location)";

      /* Only operators with the AUSPEX flag can see
       * routing information. 
       */
      if (IsOperAuspex(source_p))
      {
        sendto_one(source_p, form_str(RPL_LINKS),
                   MyConnect(source_p) ? me.name : me.id, 
                   MyConnect(source_p) ? parv[0] : ID(source_p),
	  	   target_p->name, target_p->serv->up,
                   target_p->hopcount, p);
      }
      else
      {
        sendto_one(source_p, form_str(RPL_LINKS),
                   MyConnect(source_p) ? me.name : me.id, 
                   MyConnect(source_p) ? parv[0] : ID(source_p),
	  	   target_p->name, ServerInfo.roundrobin,
                   1, p);
      }
    }
  
  sendto_one(source_p, form_str(RPL_ENDOFLINKS),
             MyConnect(source_p) ? me.name : me.id, 
             MyConnect(source_p) ? parv[0] : ID(source_p),
             EmptyString(mask) ? "*" : mask);
}

/*
 * ms_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */
static void
ms_links(struct Client *client_p, struct Client *source_p,
         int parc, char *parv[])
{
  if (hunt_server(client_p, source_p, ":%s LINKS %s :%s", 1, parc, parv)
      != HUNTED_ISME)
    return;

  if (IsClient(source_p))
    m_links(client_p,source_p,parc,parv);
}

