/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  s_user.c: User related functions.
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
 *  $Id: s_user.c,v 1.28 2004/09/03 14:18:25 nenolod Exp $
 */

#include "stdinc.h"
#include "tools.h"
#include "s_user.h"
#include "channel.h"
#include "channel_mode.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "s_bsd.h"
#include "irc_getnameinfo.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "motd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "send.h"
#include "supported.h"
#include "whowas.h"
#include "memory.h"
#include "packet.h"
#include "userhost.h"
#include "reject.h"
#include "umodes.h"
#include "hook.h"

int MaxClientCount     = 1;
int MaxConnectionCount = 1;

static BlockHeap *user_heap;

static int valid_hostname(const char *);
static int valid_username(const char *);
static void user_welcome(struct Client *);
static void report_and_set_user_flags(struct Client *, struct AccessItem *);
static int check_x_line(struct Client *, struct Client *);
static int introduce_client(struct Client *, struct Client *);

FLAG_ITEM user_mode_table[256];
char umode_list[256];

/* init_user()
 *
 * inputs       - NONE
 * output       - NONE
 * side effects - Creates a block heap for struct Users
 */
void
init_user(void)
{
  user_heap = BlockHeapCreate(sizeof(struct User), USER_HEAP_SIZE);
}

/* make_user()
 *
 * inputs       - pointer to Client struct
 * output       - pointer to struct User
 * side effects - add's an User information block to a client
 *                if it was not previously allocated.
 */
static struct User *
make_user(struct Client *client_p)
{
  if (client_p->user == NULL)
  {
    client_p->user = BlockHeapAlloc(user_heap);
    memset(client_p->user, 0, sizeof(struct User));
  }

  return(client_p->user);
}

/* free_user()
 *
 * inputs       - pointer to User struct
 *              - pointer to Client struct
 * output       - NONE
 * side effects - free an User block
 */
/* XXX - Client pointer is not really necessary */
void
free_user(struct User *user, struct Client *client_p)
{

  MyFree(user->away);
  MyFree(user->response);
  MyFree(user->auth_oper);

  /* sanity check */
  if (dlink_list_length(&user->channel) || user->invited.head || user->channel.head)
  {
    sendto_realops_flags(UMODE_ALL, L_ALL,
                         "* %#lx user (%s!%s@%s) %#lx %#lx %#lx %lu *",
                         (unsigned long)client_p, client_p ? client_p->name : "<noname>",
                         client_p->username, client_p->host, (unsigned long)user,
                         (unsigned long)user->invited.head,
                         (unsigned long)user->channel.head,
                         dlink_list_length(&user->channel));
    assert(user->invited.head == NULL);
    assert(user->channel.head == NULL);
    assert(dlink_list_length(&user->invited) == 0);
    assert(dlink_list_length(&user->channel) == 0);
  }

  BlockHeapFree(user_heap, user);
}

/* show_lusers()
 *
 * inputs       - pointer to client
 * output       - NONE
 * side effects - display to client user counts etc.
 */
void
show_lusers(struct Client *source_p) 
{
  const char *from, *to;

  if (!MyConnect(source_p) && IsCapable(source_p->from, CAP_TS6) && HasID(source_p))
  {
    from = me.id;
    to = source_p->id;
  }
  else
  {
    from = me.name;
    to = source_p->name;
  }

  sendto_one(source_p, form_str(RPL_LUSERCLIENT),
               from, to, (Count.total - Count.invisi), Count.invisi,
               dlink_list_length(&global_serv_list));

  if (Count.oper > 0)
    sendto_one(source_p, form_str(RPL_LUSEROP),
	       from, to, Count.oper);

  if (dlink_list_length(&unknown_list) > 0)
    sendto_one(source_p, form_str(RPL_LUSERUNKNOWN),
	       from, to, dlink_list_length(&unknown_list));

  if (dlink_list_length(&global_channel_list) > 0)
    sendto_one(source_p, form_str(RPL_LUSERCHANNELS),
	       from, to,
	       dlink_list_length(&global_channel_list));

  sendto_one(source_p, form_str(RPL_LUSERME),
	       from, to,
	       Count.local, Count.myserver);
  sendto_one(source_p, form_str(RPL_LOCALUSERS),
	       from, to,
	       Count.local, Count.max_loc);

  sendto_one(source_p, form_str(RPL_GLOBALUSERS),
             from, to, Count.total, Count.max_tot);

  sendto_one(source_p, form_str(RPL_STATSCONN), from, to,
	       MaxConnectionCount, MaxClientCount, Count.totalrestartcount);

  if (Count.local > MaxClientCount)
    MaxClientCount = Count.local;

  if ((Count.local + Count.myserver) > MaxConnectionCount)
    MaxConnectionCount = Count.local + Count.myserver; 
}

/* show_isupport()
 *
 * inputs       - pointer to client
 * output       - NONE
 * side effects	- display to client what we support (for them)
 */
void
show_isupport(struct Client *source_p) 
{
  char isupportbuffer[512];

  ircsprintf(isupportbuffer, FEATURES, FEATURESVALUES);
  sendto_one(source_p, form_str(RPL_ISUPPORT),
             ID_or_name(&me, source_p->from),
             ID_or_name(source_p, source_p->from),
             isupportbuffer);

  ircsprintf(isupportbuffer, FEATURES2, FEATURES2VALUES);
  sendto_one(source_p, form_str(RPL_ISUPPORT),
             ID_or_name(&me, source_p->from),
             ID_or_name(source_p, source_p->from),
             isupportbuffer);
}

/*
** register_local_user
**      This function is called when both NICK and USER messages
**      have been accepted for the client, in whatever order. Only
**      after this, is the USER message propagated.
**
**      NICK's must be propagated at once when received, although
**      it would be better to delay them too until full info is
**      available. Doing it is not so simple though, would have
**      to implement the following:
**
**      (actually it has been implemented already for a while) -orabidoo
**
**      1) user telnets in and gives only "NICK foobar" and waits
**      2) another user far away logs in normally with the nick
**         "foobar" (quite legal, as this server didn't propagate
**         it).
**      3) now this server gets nick "foobar" from outside, but
**         has alread the same defined locally. Current server
**         would just issue "KILL foobar" to clean out dups. But,
**         this is not fair. It should actually request another
**         nick from local user or kill him/her...
*/
int
register_local_user(struct Client *client_p, struct Client *source_p, 
                    const char *nick, const char *username)
{
  struct AccessItem *aconf;
  char ipaddr[HOSTIPLEN];
  int status;
  dlink_node *ptr;
  dlink_node *m;

  assert(source_p != NULL);
  assert(MyConnect(source_p));
  assert(source_p->username != username);

  if (source_p == NULL)
    return(-1);

  if (!MyConnect(source_p))
    return(-1);

  if (ConfigFileEntry.ping_cookie)
  {
    if (!IsPingSent(source_p) &&
       source_p->localClient->random_ping == 0)
    {
      source_p->localClient->random_ping = (unsigned long)rand();
      sendto_one(source_p, "PING :%lu",
                 source_p->localClient->random_ping);
      SetPingSent(source_p);
      return(-1);
    }

    if (!HasPingCookie(source_p))
      return(-1);
  }

  source_p->user->last = CurrentTime;
  /* Straight up the maximum rate of flooding... */
  source_p->localClient->allow_read = MAX_FLOOD_BURST;

  if ((status = check_client(client_p, source_p, username)) < 0)
    return(CLIENT_EXITED);

  if (valid_hostname(source_p->host) == 0)
  {
    sendto_one(source_p, ":%s NOTICE %s :*** Notice -- You have an illegal "
               "character in your hostname", me.name, source_p->name);
    strlcpy(source_p->host, source_p->localClient->sockhost,
            sizeof(source_p->host));
  }

  ptr   = source_p->localClient->confs.head;
  aconf = map_to_conf((struct ConfItem *)ptr->data);

  if (!IsGotId(source_p))
  {
    const char *p;
    unsigned int i = 0;

    if (IsNeedIdentd(aconf))
    {
      ServerStats->is_ref++;
      sendto_one(source_p, ":%s NOTICE %s :*** Notice -- You need to install "
                 "identd to use this server", me.name, source_p->name);
      exit_client(client_p, source_p, &me, "Install identd");
      return(CLIENT_EXITED);
    }

    p = username;

    if (!IsNoTilde(aconf))
      source_p->username[i++] = '~';

    while (*p && i < USERLEN)
    {
      if (*p != '[')
        source_p->username[i++] = *p;
      p++;
    }

    source_p->username[i] = '\0';
  }

  /* password check */
  if (!EmptyString(aconf->passwd) &&
      (source_p->localClient->passwd == NULL ||
      strcmp(source_p->localClient->passwd, aconf->passwd)))
  {
    ServerStats->is_ref++;
    sendto_one(source_p, form_str(ERR_PASSWDMISMATCH),
               me.name, source_p->name);
    exit_client(client_p, source_p, &me, "Bad Password");
    return(CLIENT_EXITED);
  }

  /* don't free source_p->localClient->passwd here - it can be required
   * by masked /stats I if there are auth{} blocks with need_password = no;
   * --adx
   */

  /* report if user has &^>= etc. and set flags as needed in source_p */
  report_and_set_user_flags(source_p, aconf);

  /* Limit clients -
   * We want to be able to have servers and F-line clients
   * connect, so save room for "buffer" connections.
   * Smaller servers may want to decrease this, and it should
   * probably be just a percentage of the MAXCLIENTS...
   *   -Taner
   */
  /* Except "F:" clients */
  if ((((Count.local + 1) >= (GlobalSetOptions.maxclients + MAX_BUFFER))) ||
      ((Count.local >= ServerInfo.max_clients) && !IsExemptLimits(source_p)))
  {
    sendto_realops_flags(UMODE_FULL, L_ALL,
                         "Too many clients, rejecting %s[%s].",
                         nick, source_p->host);
    ServerStats->is_ref++;
    exit_client(client_p, source_p, &me, "Sorry, server is full - try later");
    return(CLIENT_EXITED);
  }

  /* valid user name check */
  if (valid_username(source_p->username) == 0)
  {
    char tmpstr2[IRCD_BUFSIZE];

    sendto_realops_flags(UMODE_REJ, L_ALL, "Invalid username: %s (%s@%s)",
                         nick, source_p->username, source_p->host);
    ServerStats->is_ref++;
    ircsprintf(tmpstr2, "Invalid username [%s]", source_p->username);
    exit_client(client_p, source_p, &me, tmpstr2);
    return(CLIENT_EXITED);
  }

  /* end of valid user name check */
  if ((status = check_x_line(client_p, source_p)) < 0)
    return(status);

  if (IsDead(client_p))
    return(CLIENT_EXITED);

  if (source_p->id[0] == '\0') 
  {
    char *id;

    for (id = uid_get(); hash_find_id(id); id = uid_get())
      ; /* nothing */

    strlcpy(source_p->id, id, sizeof(source_p->id));
    hash_add_id(source_p);
  }

  irc_getnameinfo((struct sockaddr *)&source_p->localClient->ip,
                  source_p->localClient->ip.ss_len, ipaddr,
                  HOSTIPLEN, NULL, 0, NI_NUMERICHOST);

  sendto_realops_flags(UMODE_CCONN, L_ALL,
                       "Client connecting: %s (%s@%s) [%s] {%s} [%s]",
                       nick, source_p->username, source_p->host,
                       ConfigFileEntry.hide_spoof_ips && IsIPSpoof(source_p) ?
                       "255.255.255.255" : ipaddr, get_client_class(source_p),
                       source_p->info);

  strncpy(source_p->ipaddr, ipaddr, 256);

  hook_call_event("new_client_local", source_p);

  /* If they have died in send_* don't do anything. */
  if (IsDead(source_p))
    return(CLIENT_EXITED);

  SetUmode(source_p, UMODE_INVISIBLE);
  Count.invisi++;

  if (IsSSL(source_p))
    SetUmode(source_p, UMODE_SECURE);

  if (ConfigFileEntry.filter_on_connect != 0)
    SetUmode(source_p, UMODE_SENSITIVE);

  if ((++Count.local) > Count.max_loc)
  {
    Count.max_loc = Count.local;

    if (!(Count.max_loc % 10))
      sendto_realops_flags(UMODE_ALL, L_ALL, "New Max Local Clients: %d",
                           Count.max_loc);
  }

  SetClient(source_p);

  /* XXX source_p->servptr is &me, since local client */
  /* source_p->servptr = find_server(user->server);   */
  source_p->servptr = &me;
  dlinkAdd(source_p, &source_p->lnode, &source_p->servptr->serv->users);

  /* Increment our total user count here */
  if (++Count.total > Count.max_tot)
    Count.max_tot = Count.total;
  Count.totalrestartcount++;

  source_p->localClient->allow_read = MAX_FLOOD_BURST;

  if ((m = dlinkFindDelete(&unknown_list, source_p)) != NULL)
  {
    free_dlink_node(m);
    dlinkAdd(source_p, &source_p->localClient->lclient_node, &local_client_list);
  }
  else
  {
    sendto_realops_flags(UMODE_ALL, L_ADMIN, "Tried to register %s (%s@%s) "
                         "but I couldn't find it?!?", 
                         nick, source_p->username, source_p->host);
    exit_client(client_p, source_p, &me, "Client exited");
    return(CLIENT_EXITED);
  }

  user_welcome(source_p);
  add_user_host(source_p->username, source_p->host, 0);
  SetUserHost(source_p);

  /* This is incase you dont have a cloaking module loaded. */
  strcpy(source_p->virthost, source_p->host);

  hook_call_event("make_virthost", source_p);

  if (ServerInfo.network_cloak_on_connect)
    SetUmode(source_p, UMODE_CLOAK);

  /* Tell users what their initial mode is. */
  sendto_one(source_p, ":%s!%s@%s MODE %s :+%s", source_p->name, source_p->username,
             GET_CLIENT_HOST(source_p), source_p->name, 
             umodes_as_string(&source_p->umodes));

  return(introduce_client(client_p, source_p));
}

/* register_remote_user()
 *
 * inputs
 * output
 * side effects	- This function is called when a remote client
 *		  is introduced by a server.
 */
int
register_remote_user(struct Client *client_p, struct Client *source_p,
		     const char *username, const char *host, const char *server,
		     const char *realname, const char *ip, const char *vhost)
{
  struct Client *target_p;

  assert(source_p != NULL);
  assert(source_p->username != username);

  if (source_p == NULL)
    return(-1);

  source_p->user = make_user(source_p);

  /* coming from another server, take the servers word for it
   */
  source_p->user->server = find_server(server);
  strlcpy(source_p->host, host, sizeof(source_p->host)); 
  strlcpy(source_p->info, realname, sizeof(source_p->info));
  strlcpy(source_p->username, username, sizeof(source_p->username));

  /*
   * We have to remain compatible with older versions of the protocol.
   * Sigh... --nenolod
   */
  if (vhost != NULL)
    strlcpy(source_p->virthost, vhost, sizeof(source_p->virthost));
  else
    strlcpy(source_p->virthost, "*", 1);
  if (ip != NULL)
    strlcpy(source_p->ipaddr, ip, sizeof(source_p->ipaddr));
  else
    strlcpy(source_p->ipaddr, "0", 1);

  SetClient(source_p);

  /* Increment our total user count here */
  if (++Count.total > Count.max_tot)
    Count.max_tot = Count.total;

  source_p->user->last = CurrentTime;
  source_p->servptr    = source_p->user->server;

  /* Super GhostDetect:
   * If we can't find the server the user is supposed to be on,
   * then simply blow the user away.        -Taner
   */
  if (source_p->servptr == NULL)
  {
    sendto_realops_flags(UMODE_ALL, L_ALL,
                         "No server %s for user %s[%s@%s] from %s",
                         server, source_p->name, source_p->username,
                         source_p->host, source_p->from->name);
    kill_client(client_p, source_p, "%s (Server doesn't exist)", me.name);

    /* XXX */
    SetKilled(source_p);
    return(exit_client(NULL, source_p, &me, "Ghosted Client"));
  }

  dlinkAdd(source_p, &source_p->lnode, &source_p->servptr->serv->users);

  if ((target_p = source_p->servptr) && target_p->from != source_p->from)
  {
    sendto_realops_flags(UMODE_DEBUG, L_ALL,
                         "Bad User [%s] :%s USER %s@%s %s, != %s[%s]",
                         client_p->name, source_p->name, source_p->username,
                         source_p->host, source_p->user->server->name,
                         target_p->name, target_p->from->name);
    kill_client(client_p, source_p,
                "%s (NICK from wrong direction (%s != %s))",
                me.name, source_p->user->server->name, target_p->from->name);
    SetKilled(source_p);
    return(exit_client(source_p, source_p, &me, "USER server wrong direction"));
  }

  add_user_host(source_p->username, source_p->host, 1);
  SetUserHost(source_p);

  hook_call_event("new_client_remote", source_p);

  if (source_p->virthost[0] == '*')
  {
    strcpy(source_p->virthost, source_p->host);
    hook_call_event("make_virthost", source_p);
  }
  else
    source_p->flags |= FLAGS_USERCLOAK;

  return(introduce_client(client_p, source_p));
}

/* introduce_client()
 *
 * inputs	-
 * output	-
 * side effects - This common function introduces a client to the rest
 *		  of the net, either from a local client connect or
 *		  from a remote connect.
 */
static int
introduce_client(struct Client *client_p, struct Client *source_p)
{
  dlink_node *server_node;
  struct Client *nickservClient;

  if (!ServerInfo.hub && uplink && client_p != uplink)
  {
    if (IsCapable(uplink, CAP_TS6) && HasID(source_p))
    {
      if (IsCapable(uplink, CAP_UVH))
      {
        sendto_one(uplink, ":%s UID %s %d %lu +%s %s %s %s %s %s :%s",
                 source_p->user->server->id,
                 source_p->name, source_p->hopcount+1,
                 (unsigned long)source_p->tsinfo,
                 umodes_as_string(&source_p->umodes),
                 source_p->username, source_p->host,
                 source_p->ipaddr, source_p->id,
                 (source_p->flags & FLAGS_USERCLOAK) ? source_p->virthost : "*",
                 source_p->info);
      }
      else
      {
        sendto_one(uplink, ":%s UID %s %d %lu +%s %s %s %s %s :%s",
                 source_p->user->server->id,
                 source_p->name, source_p->hopcount+1,
		 (unsigned long)source_p->tsinfo,
                 umodes_as_string(&source_p->umodes), 
                 source_p->username, source_p->host,
		 source_p->ipaddr,
                 source_p->id, source_p->info);
      }
    }
    else
    {
      sendto_one(uplink, "NICK %s %d %lu +%s %s %s %s :%s",
                 source_p->name, source_p->hopcount+1,
		 (unsigned long)source_p->tsinfo,
                 umodes_as_string(&source_p->umodes),
                 source_p->username, source_p->host,
		 source_p->user->server->name,
                 source_p->info);
    }
    if (!IsCapable(uplink, CAP_UVH))
      if (source_p->flags & FLAGS_USERCLOAK)
        sendto_one(uplink, ":%s SVSCLOAK %s :%s", source_p->user->server->name,
	  	source_p->name, source_p->virthost);
  }
  else
  {
    DLINK_FOREACH(server_node, serv_list.head)
    {
      struct Client *server = server_node->data;

      if (IsCapable(server, CAP_LL) || server == client_p)
        continue;

      if (IsCapable(server, CAP_TS6) && HasID(source_p))
      {
        if (IsCapable(server, CAP_UVH))
        {
          sendto_one(server, ":%s UID %s %d %lu +%s %s %s %s %s %s :%s",
                   source_p->user->server->id,
                   source_p->name, source_p->hopcount+1,
                   (unsigned long)source_p->tsinfo,
                   umodes_as_string(&source_p->umodes), source_p->username, source_p->host,
                   source_p->ipaddr, source_p->id,
                   (source_p->flags & FLAGS_USERCLOAK) ? source_p->virthost : "*", source_p->info);
        }
        else
        {
          sendto_one(server, ":%s UID %s %d %lu +%s %s %s %s %s :%s",
                   source_p->user->server->id,
                   source_p->name, source_p->hopcount+1,
                   (unsigned long)source_p->tsinfo,
                   umodes_as_string(&source_p->umodes), source_p->username, source_p->host,
                   source_p->ipaddr,
                   source_p->id, source_p->info);
        }
      }
      else
        sendto_one(server, "NICK %s %d %lu +%s %s %s %s :%s",
                   source_p->name, source_p->hopcount+1,
		   (unsigned long)source_p->tsinfo,
                   umodes_as_string(&source_p->umodes), source_p->username, source_p->host,
		   source_p->user->server->name,
                   source_p->info);

    if (!IsCapable(server, CAP_UVH))
      if (source_p->flags & FLAGS_USERCLOAK)
        sendto_one(server, ":%s SVSCLOAK %s :%s", source_p->user->server->name,
                  source_p->name, source_p->virthost);
    }
  }

  /* Silly hack requested by kib of MGMServers.net. Identifies to NickServ with
   * password in source_p->localClient->passwd. Uses SIDENTIFY so that it is silent.
   * Should work with anope and shadowservices.
   */
  if (MyClient(source_p) && source_p->localClient->passwd)
  {
    if ((nickservClient = find_client("NickServ")) != NULL)
    {
      sendto_one(nickservClient, ":%s PRIVMSG NickServ :SIDENTIFY %s",
                 source_p->name, source_p->localClient->passwd);
    }
  }

  return(0);
}

/* valid_hostname()
 *
 * Inputs       - pointer to hostname
 * Output       - 1 if valid, 0 if not
 * Side effects - check hostname for validity
 *
 * NOTE: this doesn't allow a hostname to begin with a dot and
 * will not allow more dots than chars.
 */
static int
valid_hostname(const char *hostname)
{
  const char *p = hostname;

  assert(p != NULL);

  if (hostname == NULL)
    return(0);

  if (('.' == *p) || (':' == *p))
    return(0);

  while (*p)
  {
    if (!IsHostChar(*p))
      return(0);
    p++;
  }

  return(1);
}

/* valid_username()
 *
 * Inputs       - pointer to user
 * Output       - 1 if valid, 0 if not
 * Side effects - check username for validity
 *
 * Absolutely always reject any '*' '!' '?' '@' in an user name
 * reject any odd control characters names.
 * Allow '.' in username to allow for "first.last"
 * style of username
 */
static int
valid_username(const char *username)
{
  int dots      = 0;
  const char *p = username;

  assert(p != NULL);

  if (username == NULL)
    return(0);

  if ('~' == *p)
    ++p;

  /* reject usernames that don't start with an alphanum
   * i.e. reject jokers who have '-@somehost' or '.@somehost'
   * or "-hi-@somehost", "h-----@somehost" would still be accepted.
   */
  if (!IsAlNum(*p))
    return(0);

  while (*++p)
  {
    if ((*p == '.') && ConfigFileEntry.dots_in_ident)
    {
      dots++;

      if (dots > ConfigFileEntry.dots_in_ident)
        return(0);
      if (!IsUserChar(p[1]))
        return(0);
    }
    else if (!IsUserChar(*p))
      return(0);
  }

  return(1);
}

/* report_and_set_user_flags()
 *
 * inputs       - pointer to source_p
 *              - pointer to aconf for this user
 * output       - NONE
 * side effects - Report to user any special flags
 *                they are getting, and set them.
 */
static void
report_and_set_user_flags(struct Client *source_p, struct AccessItem *aconf)
{
  /* If this user is being spoofed, tell them so */
  if (IsConfDoSpoofIp(aconf))
  {
    sendto_one(source_p,
               ":%s NOTICE %s :*** Spoofing your IP. congrats.",
               me.name, source_p->name);
  }

  /* If this user is in the exception class, Set it "E lined" */
  if (IsConfExemptKline(aconf))
  {
    SetExemptKline(source_p);
    sendto_one(source_p,
               ":%s NOTICE %s :*** You are exempt from K/D/G lines. congrats.",
               me.name, source_p->name);
  }

  /* If this user is exempt from user limits set it "F lined" */
  if (IsConfExemptLimits(aconf))
  {
    SetExemptLimits(source_p);
    sendto_one(source_p,
               ":%s NOTICE %s :*** You are exempt from user limits. congrats.",
               me.name,source_p->name);
  }

  /* If this user is exempt from idle time outs */
  if (IsConfIdlelined(aconf))
  {
    SetIdlelined(source_p);
    sendto_one(source_p,
               ":%s NOTICE %s :*** You are exempt from idle limits. congrats.",
               me.name, source_p->name);
  }

  if (IsConfCanFlood(aconf))
  {
    SetCanFlood(source_p);
    sendto_one(source_p, ":%s NOTICE %s :*** You are exempt from flood "
               "protection, aren't you fearsome.",
               me.name, source_p->name);
  }
}

/* do_local_user()
 *
 * inputs	-
 * output	-
 * side effects -
 */
int
do_local_user(const char *nick, struct Client *client_p, struct Client *source_p,
              const char *username, const char *host, const char *server,
              const char *realname)
{
  assert(source_p != NULL);
  assert(source_p->username != username);

  if (source_p == NULL)
    return(0);

  if (!IsUnknown(source_p))
  {
    sendto_one(source_p, form_str(ERR_ALREADYREGISTRED),
               me.name, nick);
    return(0);
  }

  source_p->user = make_user(source_p);

  /* don't take the clients word for it, ever
   */
  source_p->user->server = &me;

  strlcpy(source_p->info, realname, sizeof(source_p->info));

  /* anti cgi:irc... brought to you by nenolod. */
  if (match("[*] *", realname)) /* [ip??-?-???-???.tu.ok.cox.net] nenolod */
    if (!IsServer(source_p))
      if (ConfigFileEntry.anti_cgi_irc == 1)
        exit_client(source_p, source_p, source_p, 
               "CGI:IRC not allowed on this server");

  if (!IsGotId(source_p)) 
  {
    /* save the username in the client
     * If you move this you'll break ping cookies..you've been warned 
     */
    strlcpy(source_p->username, username, sizeof(source_p->username));
  }

  if (source_p->name[0])
  { 
    /* NICK already received, now I have USER... */
    return(register_local_user(client_p, source_p, source_p->name, username));
  }

  return(0);
}

/* set_user_mode()
 *
 * added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
void
set_user_mode(struct Client *client_p, struct Client *source_p,
              int parc, char *parv[])
{
  int i;
  unsigned int flag;
  user_modes setflags;
  char **p;
  char *m;
  struct Client *target_p;
  int what = MODE_ADD;
  int badflag = 0; /* Only send one bad flag notice */
  dlink_node *server_node;

  if ((target_p = find_person(parv[1])) == NULL)
  {
    if (MyConnect(source_p))
      sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
                 me.name, source_p->name, parv[1]);
    return;
  }

  /* We let servers change umodes for people, for services and whatnot.
   * No need for SVSMODE --nenolod
   */
  if ((source_p != target_p || target_p->from != source_p->from) && !IsServer(source_p))
  {
     sendto_one(source_p, form_str(ERR_USERSDONTMATCH),
                me.name, source_p->name);
     return;
  }

  if (IsServer(source_p) && !MyClient(target_p))
  {
    if (!ServerInfo.hub && uplink && source_p != uplink)
    {
      sendto_one(uplink, ":%s MODE %s %s", parv[0], parv[1],
                    parv[2]);
    }
    else if (ServerInfo.hub)
    {
      DLINK_FOREACH(server_node, serv_list.head)
      {
        struct Client *server = server_node->data;

        sendto_one(server, ":%s MODE %s %s", parv[0], parv[1],
                      parv[2]);
      }
    }
  }

  if (parc < 3)
  {
    sendto_one(target_p, form_str(RPL_UMODEIS),
               me.name, source_p->name, umodes_as_string(&target_p->umodes));
    return;
  }

  /* find flags already set for user */
  CopyUmodes(setflags, target_p->umodes);

  /* parse mode change string(s) */
  for (p = &parv[2]; p && *p; p++)
  {
    for (m = *p; *m; m++)
    {
      switch (*m)
      {
        case '+':
          what = MODE_ADD;
          break;
        case '-':
          what = MODE_DEL;
          break;
        case 'a':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !HasUmode(target_p, UMODE_ADMIN))
            {
              SetUmode(target_p, UMODE_ADMIN);
            }
          }
          else
          {
            ClearUmode(target_p, UMODE_ADMIN);
          }
          break;

        case 'e':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !HasUmode(target_p, UMODE_IDENTIFY))
            {
              SetUmode(target_p, UMODE_IDENTIFY);
            }
          }
          else
          {
            ClearUmode(target_p, UMODE_IDENTIFY);
          }
          break;

        case 'N':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !HasUmode(target_p, UMODE_NETADMIN))
            {
              SetUmode(target_p, UMODE_NETADMIN);
            }
          }
          else
          {
            ClearUmode(target_p, UMODE_NETADMIN);
          }
          break;

        case 'T':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !HasUmode(target_p, UMODE_TECHADMIN))
            {
              SetUmode(target_p, UMODE_TECHADMIN);
            }
          }
          else
          {
            ClearUmode(target_p, UMODE_TECHADMIN);
          }
          break;

        case 'L':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !HasUmode(target_p, UMODE_ROUTING))
            {
              SetUmode(target_p, UMODE_ROUTING);
            }
          }
          else
          {
            ClearUmode(target_p, UMODE_ROUTING);
          }
          break;

        case 'q':
          if (what == MODE_ADD)
          {
            if (HasUmode(target_p, UMODE_NETADMIN))
            {
              SetUmode(target_p, UMODE_PROTECTED);
            }
          }
          else
          {
            ClearUmode(target_p, UMODE_PROTECTED);
          }
          break;

        case 'o':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !IsOper(target_p))
            {
              ++Count.oper;
              SetUmode(target_p, UMODE_OPER);
            }
          }
          else
          {
            /* Only decrement the oper counts if an oper to begin with
             * found by Pat Szuta, Perly , perly@xnet.com 
             */
            if (!IsOper(target_p))
              break;

            ClearUmode(target_p, UMODE_OPER);
	    ClearUmode(target_p, UMODE_ADMIN);
            ClearUmode(target_p, UMODE_NETADMIN);
            ClearUmode(target_p, UMODE_TECHADMIN);
            ClearUmode(target_p, UMODE_ROUTING);

            /* Reset user's handler. */
            target_p->handler = CLIENT_HANDLER;

            /* clear operonly usermodes for this user */
            for (i = 0; user_mode_table[i].letter; i++) {
              if (user_mode_table[i].operonly == 1) {
                ClearUmode(target_p, user_mode_table[i].mode);
              }
            }

            Count.oper--;

            if (MyConnect(target_p))
            {
              dlink_node *dm;

              detach_conf(target_p, OPER_TYPE);
              ClearOperFlags(target_p);

              if ((dm = dlinkFindDelete(&oper_list, target_p)) != NULL)
                free_dlink_node(dm);

            }
          }

          break;

        /* we may not get these,
         * but they shouldnt be in default
         */
        case ' ' :
        case '\n':
        case '\r':
        case '\t':
          break;

	case 'v':
	  if (MyClient(client_p) && MyClient(source_p) && ConfigFileEntry.disable_hostmasking != 0)
          {
            sendto_one(source_p, ":%s NOTICE %s :*** User control of hostmasking is disabled.",
                        me.name, source_p->name);
            break;
          }

        default:
          if ((flag = user_modes_from_c_to_bitmask[(unsigned char)*m]))
          {
            if (MyConnect(target_p) && !IsOper(source_p) &&
                !IsServer(client_p) &&
              (user_mode_table[flag].operonly == 1))
            {
              badflag = 1;
            }
            else
            {
              if (what == MODE_ADD)
                SetUmode(target_p, flag);
              else
                ClearUmode(target_p, flag);  
            }
          }
          else
          {
            if (MyConnect(target_p))
              badflag = 1;
          }

          break;
      }
    }
  }

  if (badflag && !IsServer(source_p))
    sendto_one(target_p, form_str(ERR_UMODEUNKNOWNFLAG),
               me.name, target_p->name);

  if (!(TestBit(setflags, UMODE_INVISIBLE)) && IsInvisible(target_p))
    ++Count.invisi;
  if ((TestBit(setflags, UMODE_INVISIBLE)) && !IsInvisible(target_p))
    --Count.invisi;

  /* compare new flags with old flags and send string which
   * will cause servers to update correctly.
   */

  send_umode_out(target_p, target_p, &setflags);
}

/* send_umode_out()
 *
 * inputs	-
 * output	- NONE
 * side effects - Only send ubuf out to servers that know about this client
 */
void
send_umode_out(struct Client *client_p, struct Client *source_p,
               user_modes *old)
{
  struct Client *target_p;
  dlink_node *ptr;

  char *mode_string = umode_difference(old, &client_p->umodes);

  if (*mode_string)
  {
    DLINK_FOREACH(ptr, serv_list.head)
    {
      target_p = ptr->data; 

      if ((target_p != client_p) && (target_p != source_p))
      {
          sendto_one(target_p, ":%s MODE %s :%s",
                     ID_or_name(source_p, target_p), ID_or_name(source_p, target_p), mode_string);
      }
    }

    if (MyClient(client_p))
      sendto_one(client_p, ":%s MODE %s :%s",
                  source_p->name, client_p->name, mode_string);

  }
}

/* user_welcome()
 *
 * inputs	- client pointer to client to welcome
 * output	- NONE
 * side effects	-
 */
static void
user_welcome(struct Client *source_p)
{
  sendto_one(source_p, form_str(RPL_WELCOME), me.name, source_p->name, 
             ServerInfo.network_name, source_p->name);
  /* This is a duplicate of the NOTICE but see below...*/
  sendto_one(source_p, form_str(RPL_YOURHOST), me.name, source_p->name,
	     me.name, ircd_version);
  
  sendto_one(source_p, form_str(RPL_CREATED),
             me.name,source_p->name, creation);
  sendto_one(source_p, form_str(RPL_MYINFO),
             me.name, source_p->name, me.name, ircd_version, umode_list);

  show_isupport(source_p);
  show_lusers(source_p);

  if (ConfigFileEntry.motd.lastChangedDate) {
     sendto_one(source_p, "NOTICE %s :*** Notice -- motd was last changed at %s",
                source_p->name, ConfigFileEntry.motd.lastChangedDate);
     sendto_one(source_p,
                "NOTICE %s :*** Notice -- Please read the motd if you haven't "
                "read it", source_p->name);
  }

  if (ConfigFileEntry.short_motd)
    send_message_file(source_p, &ConfigFileEntry.shortmotd);
  else  
    send_message_file(source_p, &ConfigFileEntry.motd);

  if (ServerInfo.wingate_enable)
  {
    sendto_one(source_p, "NOTICE %s :*** Notice -- This server conducts a security scan to prevent abuse.",
               source_p->name);
    sendto_one(source_p, "NOTICE %s :*** Notice -- If you receive connections on various ports from %s",
               source_p->name, ServerInfo.monitorbot);
    sendto_one(source_p, "NOTICE %s :*** Notice -- please disregard them as they are the monitor in action.",
               source_p->name);
    sendto_one(source_p, "NOTICE %s :*** Notice -- For more information visit %s",
               source_p->name, ServerInfo.wingate_website);
  }
}

/* check_x_line()
 *
 * inputs       - pointer to client to test
 * outupt       - -1 if exiting 0 if ok
 * side effects -
 */
static int
check_x_line(struct Client *client_p, struct Client *source_p)
{
  struct ConfItem *conf;
  struct MatchItem *match_item;
  const char *reason;

  if ((conf = find_matching_name_conf(XLINE_TYPE, source_p->info,
                                      NULL, NULL, 0)) != NULL)
  {
    match_item = (struct MatchItem *)map_to_conf(conf);
    if (match_item->reason != NULL)
      reason = match_item->reason;
    else
      reason = "No Reason";

    if (match_item->action)
    {
      if (match_item->action == 1)
      {
        sendto_realops_flags(UMODE_REJ, L_ALL,
                             "X-line Rejecting [%s] [%s], user %s [%s]",
                             source_p->info, reason,
                             get_client_name(client_p, HIDE_IP),
                             source_p->localClient->sockhost);
      }

      ServerStats->is_ref++;
      add_reject(source_p);      
      exit_client(client_p, source_p, &me, "Bad user info");
      return(CLIENT_EXITED);
    }
    else
      sendto_realops_flags(UMODE_REJ, L_ALL,
                           "X-line Warning [%s] [%s], user %s [%s]",
                           source_p->info, reason,
                           get_client_name(client_p, HIDE_IP),
                           source_p->localClient->sockhost);
  }

  return(0);
}

/* oper_up()
 *
 * inputs	- pointer to given client to oper
 * output	- NONE
 * side effects	- Blindly opers up given source_p, using aconf info
 *                all checks on passwords have already been done.
 *                This could also be used by rsa oper routines. 
 */
void
oper_up(struct Client *source_p)
{
  user_modes old;
  struct AccessItem *oconf;

  CopyUmodes(old, source_p->umodes);

  SetUmode(source_p, UMODE_OPER);

  source_p->handler = OPER_HANDLER;

  Count.oper++;

  assert(dlinkFind(&oper_list, source_p) == NULL);
  dlinkAdd(source_p, make_dlink_node(), &oper_list);

  assert(source_p->localClient->confs.head);
  oconf = map_to_conf((source_p->localClient->confs.head)->data);

  SetOFlag(source_p, oconf->port);

  SetUmode(source_p, UMODE_WALLOP);
  SetUmode(source_p, UMODE_OPERWALL);
  SetUmode(source_p, UMODE_SERVNOTICE);
  SetUmode(source_p, UMODE_LOCOPS);

  if (source_p->localClient->operflags & OPER_FLAG_TECHADMIN)
    SetUmode(source_p, UMODE_TECHADMIN);

  if (source_p->localClient->operflags & OPER_FLAG_WANTS_WHOIS)
    SetUmode(source_p, UMODE_WANTSWHOIS);

  if (source_p->localClient->operflags & OPER_FLAG_NETADMIN)
    SetUmode(source_p, UMODE_NETADMIN);

  if (source_p->localClient->operflags & OPER_FLAG_ROUTING)
    SetUmode(source_p, UMODE_ROUTING);

  if (IsOperAdmin(source_p) || IsOperHiddenAdmin(source_p))
    SetUmode(source_p, UMODE_ADMIN);
  if (!IsOperN(source_p))
    ClearUmode(source_p, UMODE_NCHANGE);

  SetUmode(source_p, UMODE_HELPOP);

  /* set the network staff virtual host. */
  /* again! we've done less crack than plexus's coding team, and got
   * hostmasking in the right PLACE.
   *      --nenolod
   */
  strncpy(source_p->virthost, ServerInfo.network_operhost, HOSTLEN);

  if (ServerInfo.network_cloak_on_oper) {
    sendto_server (NULL, source_p, NULL, NOCAPS, NOCAPS, NOFLAGS,
                   ":%s SVSCLOAK %s :%s", me.name, source_p->name,
                   source_p->virthost);

    if (!(HasUmode(source_p, UMODE_CLOAK)))
      SetUmode(source_p, UMODE_CLOAK);

    source_p->flags |= FLAGS_USERCLOAK;
  }

  sendto_realops_flags(UMODE_ALL, L_ALL, "%s (%s@%s) is now an operator",
                       source_p->name, source_p->username, GET_CLIENT_HOST(source_p));
  send_umode_out(source_p, source_p, &old);
  sendto_one(source_p, form_str(RPL_YOUREOPER), me.name, source_p->name);
  send_message_file(source_p, &ConfigFileEntry.opermotd);
}

/*
 * Quick and dirty UID code for new proposed SID on EFnet
 *
 * I like SID, we'll keep it, but this should be it's own file eventually.
 *                               --nenolod
 */

static char new_uid[TOTALSIDUID+1];     /* allow for \0 */
static void add_one_to_uid(int i);
  
/*
 * init_uid()
 * 
 * inputs	- NONE
 * output	- NONE
 * side effects	- new_uid is filled in with server id portion (sid)
 *		  (first 3 bytes) or defaulted to 'A'.
 *	          Rest is filled in with 'A'
 */

void
init_uid(void)
{
  int i;

  memset(new_uid, 0, sizeof(new_uid));

  if (ServerInfo.sid != NULL)
  {
    memcpy(new_uid, ServerInfo.sid, IRCD_MIN(strlen(ServerInfo.sid),
					     IRC_MAXSID));
    memcpy(&me.id, ServerInfo.sid, IRCD_MIN(strlen(ServerInfo.sid),
					     IRC_MAXSID));
    hash_add_id(&me);
  }

  for (i = 0; i < IRC_MAXSID; i++)
    if (new_uid[i] == '\0') 
      new_uid[i] = 'A';

  /* XXX if IRC_MAXUID != 6, this will have to be rewritten */
  memcpy(new_uid+IRC_MAXSID, "AAAAAA", IRC_MAXUID);
}

/*
 * uid_get
 *
 * inputs	- NONE
 * output	- new UID is returned to called
 * side effects	- new_uid is incremented by one.
 */

char *
uid_get(void)
{
  add_one_to_uid(TOTALSIDUID-1);	/* index from 0 */
  return(new_uid);
}

/*
 * add_one_to_uid
 *
 * inputs	- index number into new_uid
 * output	- NONE
 * side effects	- new_uid is incremented by one
 *		  note this is a recursive function
 */

static void
add_one_to_uid(int i)
{
  if (i != IRC_MAXSID)		/* Not reached server SID portion yet? */
  {
    if (new_uid[i] == 'Z')
      new_uid[i] = '0';
    else if (new_uid[i] == '9')
    {
      new_uid[i] = 'A';
      add_one_to_uid(i-1);
    }
    else new_uid[i] = new_uid[i] + 1;
  }
  else
  {
    /* XXX if IRC_MAXUID != 6, this will have to be rewritten */
    if (new_uid[i] == 'Z')
      memcpy(new_uid+IRC_MAXSID, "AAAAAA", IRC_MAXUID);
    else new_uid[i] = new_uid[i] + 1;
  }
}
