/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_map.c: Sends an Undernet compatible map to a user.
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
 *  $Id: m_map.c,v 3.3 2004/09/08 01:18:07 nenolod Exp $
 */

#include "stdinc.h"
#include "client.h"
#include "modules.h"
#include "handlers.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "ircd.h"

static void m_map(struct Client *client_p, struct Client *source_p,
                  int parc, char *parv[]);
static void dump_map(struct Client *client_p,struct Client *root, char *pbuf);

struct Message map_msgtab = {
  "MAP", 0, 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_map, m_ignore, m_map, m_ignore}
};

#ifndef STATIC_MODULES
void _modinit(void)
{
  mod_add_cmd(&map_msgtab);
}

void _moddeinit(void)
{
  mod_del_cmd(&map_msgtab);
}

const char *_version = "$Revision: 3.3 $";
#endif

static char buf[BUFSIZE];

/* m_map()
 *	parv[0] = sender prefix
 */
static void
m_map(struct Client *client_p, struct Client *source_p,
      int parc, char *parv[])
{
  if (IsOperAuspex(source_p))
  {
    dump_map(client_p, &me, buf);
    sendto_one(client_p, form_str(RPL_MAPEND), me.name, client_p->name);
  }
  else
  {
    sendto_one(source_p, form_str(ERR_NOPRIVILEGES),
               me.name, client_p->name);
  }
  return;
}

/* dump_map()
 *   dumps server map, called recursively.
 */
static void
dump_map(struct Client *client_p,struct Client *root_p, char *pbuf)
{
  int cnt = 0, i = 0, len;
  int users;
  dlink_node *ptr;
  struct Client *server_p;

  *pbuf= '\0';
       
  strncat(pbuf, root_p->name, BUFSIZE - ((size_t)pbuf - (size_t)buf));

  if (HasID(root_p))
  {
    strncat(pbuf, "[", BUFSIZE);
    strncat(pbuf, root_p->id, BUFSIZE);
    strncat(pbuf, "]", BUFSIZE);
  }

  len = strlen(buf);
  buf[len] = ' ';
	
  users = dlink_list_length(&root_p->serv->users);

  if(len < 50)
  {
    for (i = len + 1; i < 50; i++)
    {
      buf[i] = '-';
    }
  }

  snprintf(buf + 50, BUFSIZE,
           " | Users: %5lu (%4.1f%%)", dlink_list_length(&root_p->serv->users),
           100 * (float) dlink_list_length(&root_p->serv->users) / (float) Count.total);

  sendto_one(client_p, form_str(RPL_MAP), me.name, client_p->name, buf);
        
  if (root_p->serv->servers.head)
  {
    cnt += dlink_list_length(&root_p->serv->servers);

    if (cnt)
    {
      if (pbuf > buf + 3)
      {
        pbuf[-2] = ' ';
        if (pbuf[-3] == '`')
          pbuf[-3] = ' ';
      }
    }
  }

  i = 1;

  DLINK_FOREACH(ptr, root_p->serv->servers.head)
  {
    server_p = ptr->data;

    *pbuf = ' ';
    if (i < cnt)
      *(pbuf + 1) = '|';
    else
      *(pbuf + 1) = '`';
      
    *(pbuf + 2) = '-';
    *(pbuf + 3) = ' ';
    dump_map(client_p, server_p, pbuf+4);
 
    i++;
   }
}
