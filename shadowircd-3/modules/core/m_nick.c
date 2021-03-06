/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  m_nick.c: Sets a users nick.
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
 *  $Id: m_nick.c,v 1.6 2004/09/23 18:08:46 nenolod Exp $
 */

#include "stdinc.h"
#include "handlers.h"
#include "client.h"
#include "hash.h"
#include "fdlist.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_stats.h"
#include "s_user.h"
#include "whowas.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "channel.h"
#include "channel_mode.h"
#include "s_log.h"
#include "resv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "common.h"
#include "packet.h"

static void m_nick(struct Client *, struct Client *, int, char **);
static void mr_nick(struct Client *, struct Client *, int, char **);
static void ms_nick(struct Client *, struct Client *, int, char **);

static void ms_uid(struct Client *, struct Client *, int, char **);

static int nick_from_server(struct Client *, struct Client *, int, char **,
                            time_t, char *);
static int client_from_server(struct Client *, struct Client *, int, char **,
                              time_t, char *);

static int check_clean_nick(struct Client *client_p, struct Client *source_p,
                            char *nick, char *newnick,
                            struct Client *server_p);
static int check_clean_user(struct Client *client_p, char *nick, char *user,
                            struct Client *server_p);
static int check_clean_host(struct Client *client_p, char *nick, char *host,
                            struct Client *server_p);

static int clean_nick_name(char *);
static int clean_user_name(char *);
static int clean_host_name(char *);

static void perform_nick_collides(struct Client *, struct Client *,
                                  struct Client *, int, char **, time_t,
                                  char *);

struct Message nick_msgtab = {
  "NICK", 0, 0, 1, 0, MFLG_SLOW, 0,
  {mr_nick, m_nick, ms_nick, m_nick, m_ignore}
};

struct Message uid_msgtab = {
  "UID", 0, 0, 10, 0, MFLG_SLOW, 0,
  {m_ignore, m_ignore, ms_uid, m_ignore, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&nick_msgtab);
  mod_add_cmd(&uid_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&nick_msgtab);
  mod_del_cmd(&uid_msgtab);
}

const char *_version = "$Revision: 1.6 $";
const char *_desc = "Implements /nick command -- declares a client's nickname.";
#endif

/* mr_nick()
 *
 *       parv[0] = sender prefix
 *       parv[1] = nickname
 */
static void
mr_nick(struct Client *client_p, struct Client *source_p,
        int parc, char *parv[])
{
  struct Client *target_p;
  char nick[NICKLEN];
  char *s;

  if (parc < 2 || EmptyString(parv[1]))
  {
    sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN),
               me.name, EmptyString(parv[0]) ? "*" : parv[0]);
    return;
  }

  /* Terminate the nick at the first ~ */
  if ((s = strchr(parv[1], '~')) != NULL)
    *s = '\0';

  /* copy the nick and terminate it */
  strlcpy(nick, parv[1], sizeof(nick));

  /* check the nickname is ok */
  if (!clean_nick_name(nick))
  {
    sendto_one(source_p, form_str(ERR_ERRONEUSNICKNAME),
               me.name, EmptyString(parv[0]) ? "*" : parv[0], parv[1]);
    return;
  }

  /* check if the nick is resv'd */
  if (find_matching_name_conf(NRESV_TYPE, nick, NULL, NULL, 0))
  {
    sendto_one(source_p, form_str(ERR_ERRONEUSNICKNAME),
               me.name, EmptyString(parv[0]) ? "*" : parv[0], nick);
    return;
  }

  if ((target_p = find_client(nick)) == NULL)
  {
    set_initial_nick(client_p, source_p, nick);
    return;
  }
  else if (source_p == target_p)
  {
    strcpy(source_p->name, nick);
    return;
  }
  else
  {
    sendto_one(source_p, form_str(ERR_NICKNAMEINUSE), me.name, "*", nick);
  }
}

/* m_nick()
 *
 *     parv[0] = sender prefix
 *     parv[1] = nickname
 */
static void
m_nick(struct Client *client_p, struct Client *source_p,
       int parc, char *parv[])
{
  char nick[NICKLEN];
  struct Client *target_p;

  /* The following are used for checks. */
  dlink_node *lp;
  struct Membership *ms;
  struct Channel *chptr;

  if (parc < 2 || EmptyString(parv[1]))
  {
    sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN), me.name, parv[0]);
    return;
  }

  /* mark end of grace period, to prevent nickflooding */
  if (!IsFloodDone(source_p))
    flood_endgrace(source_p);

  /* terminate nick to NICKLEN */
  strlcpy(nick, parv[1], sizeof(nick));

  /* check the nickname is ok */
  if (!clean_nick_name(nick))
  {
    sendto_one(source_p, form_str(ERR_ERRONEUSNICKNAME),
               me.name, parv[0], nick);
    return;
  }

  if (find_matching_name_conf(NRESV_TYPE, nick,
                              NULL, NULL, 0) &&
      !(IsOper(source_p) && ConfigFileEntry.oper_pass_resv))
  {
    sendto_one(source_p, form_str(ERR_ERRONEUSNICKNAME),
               me.name, parv[0], nick);
    return;
  }

  if ((target_p = find_client(nick)))
  {
    /* If(target_p == source_p) the client is changing nicks between
     * equivalent nicknames ie: [nick] -> {nick}
     */

    if (target_p == source_p)
    {
      /* check the nick isnt exactly the same */
      if (strcmp(target_p->name, nick))
      {
        /* don't jump in quite yet, we got to make sure that we aren't
         * changing nicks on a stickynick channel or if we are 
         * quieted/banned or on a moderated channel.
         *                  --nenolod
         */
        DLINK_FOREACH(lp, source_p->user->channel.head)
        {
          ms = lp->data;
          chptr = ms->chptr;

          if (can_send(chptr, source_p) == CAN_SEND_NO)
          {
            /* Well, we can't send... */
            sendto_one(source_p, form_str(ERR_BANNICKCHANGE),
                       me.name, source_p->name, chptr->chname);
            return;
          }
          if (chptr->mode.mode & MODE_STICKYNICK)
          {
            sendto_one(source_p, form_str(ERR_BANNICKCHANGE),
                       me.name, source_p->name, chptr->chname);
            return;
          }
        }
        change_local_nick(client_p, source_p, nick);
        return;
      }
      else
      {
        /* client is doing :old NICK old
         * ignore it..
         */
        return;
      }
    }

    /* if the client that has the nick isnt registered yet (nick but no
     * user) then drop the unregged client
     */
    if (IsUnknown(target_p))
    {
      /* the old code had an if(MyConnect(target_p)) here.. but I cant see
       * how that can happen, m_nick() is local only --fl_
       */

      exit_client(NULL, target_p, &me, "Overridden");
      change_local_nick(client_p, source_p, nick);
      return;
    }
    else
    {
      sendto_one(source_p, form_str(ERR_NICKNAMEINUSE), me.name,
                 parv[0], nick);
      return;
    }
  }
  else
  {
      DLINK_FOREACH(lp, source_p->user->channel.head)
      {
        ms = lp->data;
        chptr = ms->chptr;

        if (can_send(chptr, source_p) == CAN_SEND_NO)
        {
          /* Well, we can't send... */
          sendto_one(source_p, form_str(ERR_BANNICKCHANGE),
                     me.name, source_p->name, chptr->chname);
          return;
        }
        if (chptr->mode.mode & MODE_STICKYNICK)
        {
          sendto_one(source_p, form_str(ERR_BANNICKCHANGE),
                     me.name, source_p->name, chptr->chname);
          return;
        }
      }
      change_local_nick(client_p, source_p, nick);
      return;
  }
}

/*
 * ms_nick()
 *      
 * server -> server nick change
 *    parv[0] = sender prefix
 *    parv[1] = nickname
 *    parv[2] = TS when nick change
 *
 * server introducing new nick
 *    parv[0] = sender prefix
 *    parv[1] = nickname
 *    parv[2] = hop count
 *    parv[3] = TS
 *    parv[4] = umode
 *    parv[5] = username
 *    parv[6] = hostname
 *    parv[7] = server
 *    parv[8] = ircname
 */
static void
ms_nick(struct Client *client_p, struct Client *source_p,
        int parc, char *parv[])
{
  struct Client *target_p;
  char nick[NICKLEN];
  time_t newts = 0;
  char *nnick = parv[1];
  char *nhop = parv[2];
  char *nts = parv[3];
  char *nusername = parv[5];
  char *nhost = parv[6];
  char *nserver = parv[7];
  char ngecos[REALLEN];

  if (parc < 2 || EmptyString(nnick))
    return;

  /* fix the length of the nick */
  strlcpy(nick, nnick, sizeof(nick));

  if (parc == 9)
  {
    struct Client *server_p = find_server(nserver);

    if (server_p == NULL)
    {
      sendto_realops_flags(UMODE_ALL, L_ALL,
                           "Invalid server %s from %s for NICK %s",
                           nserver, source_p->name, nick);

      sendto_one(client_p, ":%s KILL %s :%s (Server doesn't exist!)",
                 me.name, nick, me.name);
      return;
    }

    strlcpy(ngecos, parv[8], sizeof(ngecos));

    if (check_clean_nick(client_p, source_p, nick, nnick, server_p))
      return;

    if (check_clean_user(client_p, nick, nusername, server_p) ||
        check_clean_host(client_p, nick, nhost, server_p))
      return;

    /* check the length of the clients gecos */
    if (strlen(ngecos) > REALLEN)
    {
      sendto_realops_flags(UMODE_ALL, L_ALL,
                           "Long realname from server %s for %s",
                           nserver, nnick);
      ngecos[REALLEN] = '\0';
    }

    if (IsServer(source_p))
      newts = atol(nts);
  }
  else if (parc == 3)
  {
    if (IsServer(source_p))
      /* Server's cant change nicks.. */
      return;

    if (check_clean_nick(client_p, source_p, nick, nnick,
                         source_p->user->server))
      return;

    /*
     * Yes, this is right. HOP field is the TS field for parc = 3
     */
    newts = atol(nhop);
  }

  /* if the nick doesnt exist, allow it and process like normal */
  if (!(target_p = find_client(nick)))
  {
    nick_from_server(client_p, source_p, parc, parv, newts, nick);
    return;
  }

  /* we're not living in the past anymore, an unknown client is local only. */
  if (IsUnknown(target_p))
  {
    exit_client(NULL, target_p, &me, "Overridden");
    nick_from_server(client_p, source_p, parc, parv, newts, nick);
    return;
  }

  if (target_p == source_p)
  {
    if (strcmp(target_p->name, nick))
    {
      /* client changing case of nick */
      nick_from_server(client_p, source_p, parc, parv, newts, nick);
      return;
    }
    else
      /* client not changing nicks at all */
      return;
  }

  perform_nick_collides(source_p, client_p, target_p,
                        parc, parv, newts, nick);
}

/* ms_uid()
 *
 *  parv[0] = sender prefix
 *  parv[1] = nickname
 *  parv[2] = hop count
 *  parv[3] = TS
 *  parv[4] = umode
 *  parv[5] = username
 *  parv[6] = hostname
 *  parv[7] = ip
 *  parv[8] = uid
 *  -- if not uvh --
 *  parv[9] = ircname (gecos)
 *  -- else --
 *  parv[9] = vhost (* if it is a normal vhost)
 *  parv[10] = ircname [gecos]
 */
static void
ms_uid(struct Client *client_p, struct Client *source_p,
       int parc, char *parv[])
{
  struct Client *target_p;
  char nick[NICKLEN];
  time_t newts = 0;
#define ID_SID		parv[0]
#define ID_NICK		parv[1]
#define ID_HOP		parv[2]
#define ID_TS		parv[3]
#define ID_USERMODES	parv[4]
#define ID_USERNAME	parv[5]
#define ID_HOSTNAME	parv[6]
#define ID_IP		parv[7]
#define ID_ID		parv[8]
#define ID_VHOST        parv[9]
#define ID_GECOS        parv[10]

  /* XXX can this happen ? */
  if (EmptyString(ID_NICK))
    return;

  /* parse the nickname */
  strlcpy(nick, ID_NICK, sizeof(nick));

  /* check the nicknames, usernames and hostnames are ok */
  if (check_clean_nick(client_p, source_p, nick, ID_NICK, source_p) ||
      check_clean_user(client_p, nick, ID_USERNAME, source_p) ||
      check_clean_host(client_p, nick, ID_HOSTNAME, source_p))
    return;

  /* some systems strlen functions, i.e. linux like to go over bounds
   * thus poisoning the memory stack, and causing the ircd to crash.
   * therefore, we now do this blindly to promote maximum stability.
   *   --nenolod
   */
  if (IsCapable(client_p, CAP_UVH))
    ID_GECOS[REALLEN] = '\0';
  else
    ID_VHOST[REALLEN] = '\0';

  newts = atol(ID_TS);

  /* if there is an ID collision, kill our client, and kill theirs.
   * this may generate 401's, but it ensures that both clients always
   * go, even if the other server refuses to do the right thing.
   */

  if ((target_p = hash_find_id(ID_ID)) != NULL)
  {
    sendto_realops_flags(UMODE_ALL, L_ALL,
                         "ID collision on %s(%s <- %s)(both killed)",
                         target_p->name, target_p->from->name,
                         client_p->name);

    kill_client_ll_serv_butone(NULL, target_p, "%s (ID collision)", me.name);

    ServerStats->is_kill++;

    SetKilled(target_p);
    exit_client(client_p, target_p, &me, "ID Collision");
    return;
  }

  if ((target_p = find_client(ID_NICK)) == NULL)
    client_from_server(client_p, source_p, parc, parv, newts, nick);
  else if (IsUnknown(target_p))
  {
    exit_client(NULL, target_p, &me, "Overridden");
    client_from_server(client_p, source_p, parc, parv, newts, nick);
  }
  else
    perform_nick_collides(source_p, client_p, target_p,
                          parc, parv, newts, nick);
}

/* check_clean_nick()
 *
 * input	- pointer to source
 *		- 
 *		- nickname
 *		- truncated nickname
 *		- origin of client
 *		- pointer to server nick is coming from
 * output	- none
 * side effects - if nickname is erroneous, or a different length to
 *                truncated nickname, return 1
 */
static int
check_clean_nick(struct Client *client_p, struct Client *source_p,
                 char *nick, char *newnick, struct Client *server_p)
{
  /* the old code did some wacky stuff here, if the nick is invalid, kill it
   * and dont bother messing at all
   */

  if (!clean_nick_name(nick) || strcmp(nick, newnick))
  {
    ServerStats->is_kill++;
    sendto_realops_flags(UMODE_DEBUG, L_ALL,
                         "Bad Nick: %s From: %s(via %s)",
                         nick, server_p->name, client_p->name);

    sendto_one(client_p, ":%s KILL %s :%s (Bad Nickname)",
               me.name, newnick, me.name);

    /* bad nick change */
    if (source_p != client_p)
    {
      kill_client_ll_serv_butone(client_p, source_p,
                                 "%s (Bad Nickname)", me.name);
      SetKilled(source_p);
      exit_client(client_p, source_p, &me, "Bad Nickname");
    }

    return (1);
  }

  return (0);
}

/* check_clean_user()
 * 
 * input	- pointer to client sending data
 *              - nickname
 *              - username to check
 *		- origin of NICK
 * output	- none
 * side effects - if username is erroneous, return 1
 */
static int
check_clean_user(struct Client *client_p, char *nick,
                 char *user, struct Client *server_p)
{
  if (strlen(user) > USERLEN)
  {
    ServerStats->is_kill++;
    sendto_realops_flags(UMODE_DEBUG, L_ALL,
                         "Long Username: %s Nickname: %s From: %s(via %s)",
                         user, nick, server_p->name, client_p->name);

    sendto_one(client_p, ":%s KILL %s :%s (Bad Username)",
               me.name, nick, me.name);

    return (1);
  }

  if (!clean_user_name(user))
    sendto_realops_flags(UMODE_DEBUG, L_ALL,
                         "Bad Username: %s Nickname: %s From: %s(via %s)",
                         user, nick, server_p->name, client_p->name);

  return (0);
}

/* check_clean_host()
 * 
 * input	- pointer to client sending us data
 *              - nickname
 *              - hostname to check
 *		- source name
 * output	- none
 * side effects - if hostname is erroneous, return 1
 */
static int
check_clean_host(struct Client *client_p, char *nick,
                 char *host, struct Client *server_p)
{
  if (strlen(host) > HOSTLEN)
  {
    ServerStats->is_kill++;
    sendto_realops_flags(UMODE_DEBUG, L_ALL,
                         "Long Hostname: %s Nickname: %s From: %s(via %s)",
                         host, nick, server_p->name, client_p->name);

    sendto_one(client_p, ":%s KILL %s :%s (Bad Hostname)",
               me.name, nick, me.name);

    return (1);
  }

  if (!clean_host_name(host))
    sendto_realops_flags(UMODE_DEBUG, L_ALL,
                         "Bad Hostname: %s Nickname: %s From: %s(via %s)",
                         host, nick, server_p->name, client_p->name);

  return (0);
}

/* clean_nick_name()
 *
 * input	- nickname
 * output	- none
 * side effects - walks through the nickname, returning 0 if erroneous
 */
static int
clean_nick_name(char *nick)
{
  assert(nick);
  if (nick == NULL)
    return (0);

  /* nicks cant start with a digit or - or be 0 length */
  /* This closer duplicates behaviour of hybrid-6 */

  if (*nick == '-' || IsDigit(*nick) || *nick == '\0')
    return (0);

  for (; *nick; nick++)
  {
    if (!IsNickChar(*nick))
      return (0);
  }

  return (1);
}

/* clean_user_name()
 *
 * input	- username
 * output	- none
 * side effects - walks through the username, returning 0 if erroneous
 */
static int
clean_user_name(char *user)
{
  assert(user);
  if (user == NULL)
    return 0;

  for (; *user; user++)
  {
    if (!IsUserChar(*user))
      return 0;
  }

  return 1;
}

/* clean_host_name()
 * input	- hostname
 * output	- none
 * side effects - walks through the hostname, returning 0 if erroneous
 */
static int
clean_host_name(char *host)
{
  assert(host);
  if (host == NULL)
    return 0;
  for (; *host; host++)
  {
    if (!IsHostChar(*host))
      return 0;
  }

  return 1;
}

/*
 * nick_from_server()
 */
static int
nick_from_server(struct Client *client_p, struct Client *source_p, int parc,
                 char *parv[], time_t newts, char *nick)
{
  if (IsServer(source_p))
  {
    /* A server introducing a new client, change source */
    source_p = make_client(client_p);
    dlinkAdd(source_p, &source_p->node, &global_client_list);

    /* We don't need to introduce leafs clients back to them! */

    if (parc > 2)
      source_p->hopcount = atoi(ID_HOP);
    if (newts)
      source_p->tsinfo = newts;
    else
    {
      newts = source_p->tsinfo = CurrentTime;
      ts_warn("Remote nick %s (%s) introduced without a TS", nick, parv[0]);
    }

    /* copy the nick in place */
    strcpy(source_p->name, nick);
    hash_add_client(source_p);

    if (parc > 8)
    {
      unsigned int flag;
      char *m;

      /* parse usermodes */
      m = &parv[4][1];

  while (*m)
  {
    flag = user_modes_from_c_to_bitmask[(unsigned char)*m];
    if (!HasUmode(source_p, flag)) {
      SetUmode(source_p, flag);
      if (flag == UMODE_OPER)
        Count.oper++;
      if (flag == UMODE_INVISIBLE)
        Count.invisi++;
    } else {
      sendto_realops_flags(UMODE_DEBUG, L_ALL,
          "Major WTF: Duplicate flag detected in nick_from_server for %s -- umode string = %s",
          nick, parv[4]);
    }
    m++;
  }


      return (register_remote_user
              (client_p, source_p, parv[5], parv[6], parv[7],
               parv[8], "0.0.0.0", "*"));
    }
  }
  else if (source_p->name[0])
  {
    /* client changing their nick */
    if (irccmp(parv[0], nick))
      source_p->tsinfo = newts ? newts : CurrentTime;
    sendto_common_channels_local(source_p, 1,
                                 ":%s!%s@%s NICK :%s",
                                 source_p->name,
                                 source_p->username,
                                 GET_CLIENT_HOST(source_p), nick);
    if (source_p->user != NULL)
    {
      add_history(source_p, 1);
      sendto_server(client_p, CAP_TS6, NOCAPS,
                    ":%s NICK %s :%lu", ID(source_p), nick,
                    (unsigned long)source_p->tsinfo);
      sendto_server(client_p, NOCAPS, CAP_TS6,
                    ":%s NICK %s :%lu", parv[0], nick,
                    (unsigned long)source_p->tsinfo);
    }
  }

  /* set the new nick name */
  if (source_p->name[0])
    hash_del_client(source_p);
  strcpy(source_p->name, nick);
  hash_add_client(source_p);
  /* remove all accepts pointing to the client */
  del_all_accepts(source_p);
  return (0);
}

/*
 * client_from_server()
 */
static int
client_from_server(struct Client *client_p,
                   struct Client *source_p, int parc,
                   char *parv[], time_t newts, char *nick)
{
  char *m;
  unsigned int flag;
  const char *id = ID_ID;
  const char *servername = source_p->name;
  const char *gecos = (parc > 10) ? ID_GECOS : ID_VHOST;
  const char *vhost = (parc > 10) ? ID_VHOST : "*";
  const char *ip = ID_IP;
  source_p = make_client(client_p);
  dlinkAdd(source_p, &source_p->node, &global_client_list);
  /* We don't need to introduce leafs clients back to them! */
  source_p->hopcount = atoi(ID_HOP);
  source_p->tsinfo = newts;
  /* copy the nick in place */
  strcpy(source_p->name, nick);
  strlcpy(source_p->id, id, sizeof(source_p->id));
  hash_add_client(source_p);
  hash_add_id(source_p);
  /* parse usermodes */
  m = &parv[4][1];
  while (*m)
  {
    flag = user_modes_from_c_to_bitmask[(unsigned char)*m];
    if (!HasUmode(source_p, flag)) {
      SetUmode(source_p, flag);
      if (flag == UMODE_OPER)
        Count.oper++;
      if (flag == UMODE_INVISIBLE)
        Count.invisi++;
    } else {
      sendto_realops_flags(UMODE_DEBUG, L_ALL,
          "Major WTF: Duplicate flag detected in client_from_server for %s -- umode string = %s",
          nick, parv[4]);
    }
    m++;
  }


  return (register_remote_user
          (client_p, source_p, parv[5], parv[6], servername,
           gecos, ip, vhost));
}

static void
perform_nick_collides(struct Client *source_p,
                      struct Client *client_p,
                      struct Client *target_p, int parc,
                      char *parv[], time_t newts, char *nick)
{
  int sameuser;
  /* server introducing new nick */
  if (IsServer(source_p))
  {
    /* if we dont have a ts, or their TS's are the same, kill both */
    if (!newts || !target_p->tsinfo || (newts == target_p->tsinfo))
    {
      sendto_realops_flags(UMODE_ALL, L_ALL,
                           "Nick collision on %s(%s <- %s)(both killed)",
                           target_p->name, target_p->from->name,
                           client_p->name);
      kill_client_ll_serv_butone(NULL, target_p,
                                 "%s (Nick collision (new))", me.name);
      ServerStats->is_kill++;
      sendto_one(target_p, form_str(ERR_NICKCOLLISION), me.name,
                 target_p->name, target_p->name);
      SetKilled(target_p);
      exit_client(client_p, target_p, &me, "Nick collision (new)");
      return;
    }
    /* the timestamps are different */
    else
    {
      sameuser = (target_p->user)
        && !irccmp(target_p->username, parv[5])
        && !irccmp(target_p->host, parv[6]);
        if (sameuser)
          sendto_realops_flags(UMODE_ALL, L_ALL,
                               "Nick collision on %s(%s <- %s)(older killed)",
                               target_p->name, target_p->from->name,
                               client_p->name);
        else
          sendto_realops_flags(UMODE_ALL, L_ALL,
                               "Nick collision on %s(%s <- %s)(newer killed)",
                               target_p->name, target_p->from->name,
                               client_p->name);
        ServerStats->is_kill++;
        sendto_one(target_p, form_str(ERR_NICKCOLLISION),
                   me.name, target_p->name, target_p->name);
        /* if it came from a LL server, itd have been source_p,
         * so we dont need to mark target_p as known
         */
        kill_client_ll_serv_butone(source_p, target_p,
                                   "%s (Nick collision (new))", me.name);
        SetKilled(target_p);
        exit_client(client_p, target_p, &me, "Nick collision");
        if (parc == 9)
          nick_from_server(client_p, source_p, parc, parv, newts, nick);
        else if (parc > 9) /* set the new user object up */
          client_from_server(client_p, source_p, parc, parv, newts, nick);
        return;
    }
  }

  /* its a client changing nick and causing a collide */
  if (!newts || !target_p->tsinfo
      || (newts == target_p->tsinfo) || !source_p->user)
  {
    sendto_realops_flags(UMODE_ALL, L_ALL,
                         "Nick change collision from %s to %s(%s <- %s)(both killed)",
                         source_p->name, target_p->name,
                         target_p->from->name, client_p->name);
    ServerStats->is_kill++;
    sendto_one(target_p, form_str(ERR_NICKCOLLISION), me.name,
               target_p->name, target_p->name);
    /* if we got the message from a LL, it knows about source_p */
    kill_client_ll_serv_butone(NULL, source_p,
                               "%s (Nick change collision)", me.name);
    ServerStats->is_kill++;
    /* If we got the message from a LL, ensure it gets the kill */
    kill_client_ll_serv_butone(NULL, target_p,
                               "%s (Nick change collision)", me.name);
    SetKilled(target_p);
    exit_client(NULL, target_p, &me, "Nick collision(new)");
    SetKilled(source_p);
    exit_client(client_p, source_p, &me, "Nick collision(old)");
    return;
  }
  else
  {
    sameuser = !irccmp(target_p->username, source_p->username)
      && !irccmp(target_p->host, source_p->host);
    if ((sameuser && newts < target_p->tsinfo)
        || (!sameuser && newts > target_p->tsinfo))
    {
      if (sameuser)
        sendto_realops_flags(UMODE_ALL, L_ALL,
                             "Nick change collision from %s to %s(%s <- %s)(older killed)",
                             source_p->name, target_p->name,
                             target_p->from->name, client_p->name);
      else
        sendto_realops_flags(UMODE_ALL, L_ALL,
                             "Nick change collision from %s to %s(%s <- %s)(newer killed)",
                             source_p->name, target_p->name,
                             target_p->from->name, client_p->name);
      ServerStats->is_kill++;
      /* this won't go back to the incoming link, so LL doesnt matter */
      kill_client_ll_serv_butone(client_p, source_p,
                                 "%s (Nick change collision)", me.name);
      SetKilled(source_p);
      if (sameuser)
        exit_client(client_p, source_p, &me, "Nick collision(old)");
      else
        exit_client(client_p, source_p, &me, "Nick collision(new)");
      return;
    }
    else
    {
      if (sameuser)
        sendto_realops_flags(UMODE_ALL, L_ALL,
                             "Nick collision on %s(%s <- %s)(older killed)",
                             target_p->name, target_p->from->name,
                             client_p->name);
      else
        sendto_realops_flags(UMODE_ALL, L_ALL,
                             "Nick collision on %s(%s <- %s)(newer killed)",
                             target_p->name, target_p->from->name,
                             client_p->name);
      kill_client_ll_serv_butone(source_p, target_p,
                                 "%s (Nick collision)", me.name);
      ServerStats->is_kill++;
      sendto_one(target_p, form_str(ERR_NICKCOLLISION), me.name,
                 target_p->name, target_p->name);
      SetKilled(target_p);
      exit_client(client_p, target_p, &me, "Nick collision");
    }
  }

  /* we should only ever call nick_from_server() here, as
   * this is a client changing nick, not a new client
   */
  nick_from_server(client_p, source_p, parc, parv, newts, nick);
}
