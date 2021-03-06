/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_silence.c: Silence list manipulation.
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
 *  $Id: m_silence.c,v 3.3 2004/09/08 01:18:08 nenolod Exp $
 */

#include "stdinc.h"
#include "tools.h"
#include "common.h"  
#include "handlers.h"
#include "client.h"
#include "client_silence.h"
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

static void m_silence(struct Client*, struct Client*, int, char**);


struct Message silence_msgtab = {
  "SILENCE", 0, 0, 0, 0, MFLG_SLOW, 0L,
  {m_unregistered, m_silence, m_silence, m_silence, m_silence}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&silence_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&silence_msgtab);
}

const char *_version = "$Revision: 3.3 $";
#endif

/* m_silence
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
static void
m_silence(struct Client *client_p, struct Client *source_p,
        int parc, char *parv[])
{
  char c, *cp;
  dlink_node *silence;
  struct Ban *actualSilence;

  if (!MyClient(source_p))
    return;

  if (parc < 2) {
    DLINK_FOREACH(silence, source_p->silence_list.head) {
      actualSilence = silence->data;
      sendto_one(source_p, form_str(RPL_SILELIST),
                 me.name, source_p->name, actualSilence->banstr);
    }
    sendto_one(source_p, form_str(RPL_ENDOFSILELIST), me.name, source_p->name);
    return;
  } else {
    cp = parv[1];
    c = *cp;

    if (c == '+' || c == '-')
      cp++;
    else {
      sendto_one(source_p, ":%s NOTICE %s :Type /SILENCE [+|-]<mask> to add or delete entries from"
                    " your silence list.", me.name, source_p->name);
      return;
    }

    if (c == '+')
      add_silence(source_p, cp);
    else
      del_silence(source_p, cp);
  }
}
