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
 *  $Id: s_user.c,v 1.38 2004/04/06 19:23:57 nenolod Exp $
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


int MaxClientCount     = 1;
int MaxConnectionCount = 1;

static BlockHeap *user_heap;

static int valid_hostname(const char *);
static int valid_username(const char *);
static void user_welcome(struct Client *);
static void report_and_set_user_flags(struct Client *, struct AccessItem *);
static int check_x_line(struct Client *, struct Client *);
static int introduce_client(struct Client *, struct Client *);

/* table of ascii char letters
 * to corresponding bitmask
 */
static const struct flag_item
{
  const unsigned int mode;
  const unsigned char letter;
} user_modes[] = {
  { UMODE_ADMIN,      'a' },
  { UMODE_BOTS,       'b' },
  { UMODE_CCONN,      'c' },
  { UMODE_DEBUG,      'd' },
  { UMODE_FULL,       'f' },
  { UMODE_CALLERID,   'g' },
  { UMODE_HIDEOPER,   'h' },
  { UMODE_INVISIBLE,  'i' },
  { UMODE_LOCOPS,     'l' },
  { UMODE_NCHANGE,    'n' },
  { UMODE_OPER,       'o' },
  { UMODE_IDENTIFY,   'r' },
  { UMODE_SERVNOTICE, 's' },
  { UMODE_UNAUTH,     'u' },
  { UMODE_CLOAK,      'v' },
  { UMODE_WALLOP,     'w' },
  { UMODE_EXTERNAL,   'x' },
  { UMODE_SPY,        'y' },
  { UMODE_OPERWALL,   'z' },
  { UMODE_SVSADMIN,   'A' },
  { UMODE_PMFILTER,   'E' },
  { UMODE_HELPOP,     'H' },
  { UMODE_BLOCKINVITE, 'I' },
  { UMODE_NETADMIN,   'N' },
  { UMODE_SVSOPER,    'O' },
  { UMODE_SVSROOT,    'R' },
  { UMODE_TECHADMIN,  'T' },
  { UMODE_WHOIS,      'W' },
  { UMODE_INVISIBILITY, 'X' },
  { UMODE_SECURE,     'Z' },
  { UMODE_DEAF,       'D' },
  { 0, '\0' }
};

/* memory is cheap. map 0-255 to equivalent mode */
const unsigned int user_modes_from_c_to_bitmask[] =
{
  /* 0x00 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x0F */
  /* 0x10 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x1F */
  /* 0x20 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x2F */
  /* 0x30 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x3F */
  0,                /* @ */
  UMODE_SVSADMIN,   /* A */
  0,                /* B */
  0,                /* C */
  UMODE_DEAF,       /* D */
  UMODE_PMFILTER,   /* E */
  0,                /* F */
  0,                /* G */
  UMODE_HELPOP,     /* H */
  UMODE_BLOCKINVITE, /* I */
  0,                /* J */
  0,                /* K */
  0,                /* L */
  0,                /* M */
  UMODE_NETADMIN,   /* N */
  UMODE_SVSOPER,    /* O */
  0,                /* P */
  0,                /* Q */
  UMODE_SVSROOT,    /* R */
  0,                /* S */
  UMODE_TECHADMIN,  /* T */
  0,                /* U */
  0,                /* V */
  UMODE_WHOIS,      /* W */
  UMODE_INVISIBILITY, /* X */
  0,                /* Y */
  UMODE_SECURE,     /* Z 0x5A */
  0, 0, 0, 0, 0,    /* 0x5F   */ 
  0,                /* 0x60   */
  UMODE_ADMIN,      /* a */
  UMODE_BOTS,       /* b */
  UMODE_CCONN,      /* c */
  UMODE_DEBUG,      /* d */
  0,                /* e */
  UMODE_FULL,       /* f */
  UMODE_CALLERID,   /* g */
  UMODE_HIDEOPER,   /* h */
  UMODE_INVISIBLE,  /* i */
  0,                /* j */
  0,                /* k */
  UMODE_LOCOPS,     /* l */
  0,                /* m */
  UMODE_NCHANGE,    /* n */
  UMODE_OPER,       /* o */
  0,                /* p */
  0,                /* q */
  UMODE_IDENTIFY,   /* r */
  UMODE_SERVNOTICE, /* s */
  0,                /* t */
  UMODE_UNAUTH,     /* u */
  UMODE_CLOAK,      /* v */
  UMODE_WALLOP,     /* w */
  UMODE_EXTERNAL,   /* x */
  UMODE_SPY,        /* y */
  UMODE_OPERWALL,   /* z      0x7A */
  0,0,0,0,0,        /* 0x7B - 0x7F */

  /* 0x80 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x8F */
  /* 0x90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x9F */
  /* 0xA0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xAF */
  /* 0xB0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xBF */
  /* 0xC0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xCF */
  /* 0xD0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xDF */
  /* 0xE0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xEF */
  /* 0xF0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  /* 0xFF */
};

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
               from, to, (Count.total-Count.invisi),
               Count.invisi, dlink_list_length(&global_serv_list));

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
      sendto_one(source_p, "NOTICE %s :*** If you are having problems connecting "
                   "due to ping timeouts, please type /quote PONG :%lu now.",
                   source_p->name, source_p->localClient->random_ping);
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

    sendto_realops_flags(UMODE_ADMIN, L_ALL, "Invalid username: %s (%s@%s)",
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

  /* If they have died in send_* don't do anything. */
  if (IsDead(source_p))
    return(CLIENT_EXITED);

  source_p->umodes |= UMODE_INVISIBLE;
  Count.invisi++;

  if (IsSSL(source_p))
    source_p->umodes |= UMODE_SECURE;

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

  make_virthost(source_p->host, source_p->virthost);

  if (ServerInfo.network_cloak_on_connect)
    source_p->umodes |= UMODE_CLOAK;

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
		     const char *realname)
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

  make_virthost(source_p->host, source_p->virthost);

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
  static char ubuf[12];

  if (MyClient(source_p))
    send_umode(source_p, source_p, 0, SEND_UMODES, ubuf);
  else
    send_umode(NULL, source_p, 0, SEND_UMODES, ubuf);

  if (!*ubuf)
  {
    ubuf[0] = '+';
    ubuf[1] = '\0';
  }

  /* arghhh one could try not introducing new nicks to ll leafs
   * but then you have to introduce them "on the fly" in SJOIN
   * not fun.
   * Its not going to cost much more bandwidth to simply let new
   * nicks just ride on through.
   */

  /* We now introduce nicks "on the fly" in SJOIN anyway --
   * you _need_ to if you aren't going to burst everyone initially.
   *
   * Only send to non CAP_LL servers, unless we're a lazylink leaf,
   * in that case just send it to the uplink.
   * -davidt
   * rewritten to cope with SIDs .. eww eww eww --is
   */

  /* XXX THESE NEED A PREFIX!?!?!? */
  if (!ServerInfo.hub && uplink && IsCapable(uplink, CAP_LL) &&
      client_p != uplink)
  {
    if (IsCapable(uplink, CAP_TS6) && HasID(source_p))
    {
      sendto_one(uplink, ":%s UID %s %d %lu %s %s %s %s %s :%s",
                 source_p->user->server->id,
                 source_p->name, source_p->hopcount+1,
		 (unsigned long)source_p->tsinfo,
                 ubuf, source_p->username, source_p->host,
		 (MyClient(source_p)?source_p->localClient->sockhost:"0"),
                 source_p->id, source_p->info);
    }
    else
    {
      sendto_one(uplink, "NICK %s %d %lu %s %s %s %s :%s",
                 source_p->name, source_p->hopcount+1,
		 (unsigned long)source_p->tsinfo,
                 ubuf, source_p->username, source_p->host,
		 source_p->user->server->name,
                 source_p->info);
    }
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
        sendto_one(server, ":%s UID %s %d %lu %s %s %s %s %s :%s",
                   source_p->user->server->id,
                   source_p->name, source_p->hopcount+1,
		   (unsigned long)source_p->tsinfo,
                   ubuf, source_p->username, source_p->host,
		   (MyClient(source_p)?source_p->localClient->sockhost:"0"),
                   source_p->id, source_p->info);
      else
        sendto_one(server, "NICK %s %d %lu %s %s %s %s :%s",
                   source_p->name, source_p->hopcount+1,
		   (unsigned long)source_p->tsinfo,
                   ubuf, source_p->username, source_p->host,
		   source_p->user->server->name,
                   source_p->info);
    if (source_p->flags & FLAGS_USERCLOAK)
      sendto_one(server, ":%s SVSCLOAK %s :%s", source_p->user->server->name,
                source_p->name, source_p->virthost);
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
  unsigned int i;
  unsigned int flag;
  unsigned int setflags;
  char **p;
  char *m;
  struct Client *target_p;
  int what = MODE_ADD;
  int badflag = 0; /* Only send one bad flag notice */
  char buf[BUFSIZE];

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

  /* Pass on services mode changes... */
  if (!MyClient(target_p))
  {
     sendto_server(NULL, source_p, NULL, 0, 0, 0, ":%s MODE %s %s",
      source_p->name, target_p->name, parv[2]);
     return;
  }

  if (parc < 3)
  {
    m = buf;
    *m++ = '+';

    for (i = 0; user_modes[i].letter && (m - buf < BUFSIZE - 4); i++)
      if (target_p->umodes & user_modes[i].mode)
        *m++ = user_modes[i].letter;
    *m = '\0';

    sendto_one(target_p, form_str(RPL_UMODEIS),
               me.name, source_p->name, buf);
    return;
  }

  /* find flags already set for user */
  setflags = target_p->umodes;

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
        case 'o':
          if (what == MODE_ADD)
          {
            if (IsServer(client_p) && !IsOper(target_p))
            {
              ++Count.oper;
              SetOper(target_p);
            }
          }
          else
          {
            /* Only decrement the oper counts if an oper to begin with
             * found by Pat Szuta, Perly , perly@xnet.com 
             */
            if (!IsOper(target_p))
              break;

            ClearOper(target_p);
            target_p->umodes &= ~ConfigFileEntry.oper_only_umodes;
            Count.oper--;

            if (MyConnect(source_p))
            {
              dlink_node *dm;

              detach_conf(target_p, OPER_TYPE);
              ClearOperFlags(target_p);

              if ((dm = dlinkFindDelete(&oper_list, target_p)) != NULL)
                free_dlink_node(dm);
            }
          }

        /* we may not get these,
         * but they shouldnt be in default
         */
        case ' ' :
        case '\n':
        case '\r':
        case '\t':
          break;

        case 'N':
        case 'T':
          if (!IsOper(source_p) || source_p->localClient->operflags & OPER_FLAG_NETADMIN)
            break;
	/*
	 * just some quick checks on services modes
	 */
        case 'A':
        case 'O':
        case 'R':
	case 'e':
          if (!IsServer(source_p))
             break;

        default:
          if ((flag = user_modes_from_c_to_bitmask[(unsigned char)*m]))
          {
            if (MyConnect(target_p) && !IsOper(target_p) &&
                (ConfigFileEntry.oper_only_umodes & flag))
            {
              badflag = 1;
            }
            else
            {
              if (what == MODE_ADD)
                target_p->umodes |= flag;
              else
                target_p->umodes &= ~flag;  
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

  if ((target_p->umodes & UMODE_NCHANGE) && !IsOperN(target_p))
  {
    sendto_one(target_p, ":%s NOTICE %s :*** You need nick_changes = yes;",
               me.name, target_p->name);
    target_p->umodes &= ~UMODE_NCHANGE; /* only tcm's really need this */
  }

  if (MyConnect(source_p) && (source_p->umodes & UMODE_ADMIN) &&
      !IsOperAdmin(source_p) && !IsOperHiddenAdmin(source_p))
  {
    sendto_one(target_p, ":%s NOTICE %s :*** You need admin = yes;",
               me.name, target_p->name);
    target_p->umodes &= ~UMODE_ADMIN;
  }

  if (!(setflags & UMODE_INVISIBLE) && IsInvisible(target_p))
    ++Count.invisi;
  if ((setflags & UMODE_INVISIBLE) && !IsInvisible(target_p))
    --Count.invisi;

  /* compare new flags with old flags and send string which
   * will cause servers to update correctly.
   */
  send_umode_out(target_p, target_p, setflags);
}

/* send_umode()
 * send the MODE string for user (user) to connection client_p
 * -avalon
 */
void
send_umode(struct Client *client_p, struct Client *source_p,
           unsigned int old, unsigned int sendmask, char *umode_buf)
{
  int what = 0;
  unsigned int i;
  unsigned int flag;
  char *m = umode_buf;

  /* build a string in umode_buf to represent the change in the user's
   * mode between the new (source_p->umodes) and 'old'.
   */
  for (i = 0; user_modes[i].letter; i++)
  {
    flag = user_modes[i].mode;

    if (MyClient(source_p) && !(flag & sendmask))
      continue;

    if ((flag & old) && !(source_p->umodes & flag))
    {
      if (what == MODE_DEL)
        *m++ = user_modes[i].letter;
      else
      {
        what = MODE_DEL;
        *m++ = '-';
        *m++ = user_modes[i].letter;
      }
    }
    else if (!(flag & old) && (source_p->umodes & flag))
    {
      if (what == MODE_ADD)
        *m++ = user_modes[i].letter;
      else
      {
        what = MODE_ADD;
        *m++ = '+';
        *m++ = user_modes[i].letter;
      }
    }
  }

  *m = '\0';

  if (*umode_buf && client_p)
    sendto_one(client_p, ":%s MODE %s :%s",
               source_p->name, source_p->name, umode_buf);
}

/* send_umode_out()
 *
 * inputs	-
 * output	- NONE
 * side effects - Only send ubuf out to servers that know about this client
 */
void
send_umode_out(struct Client *client_p, struct Client *source_p,
               unsigned int old)
{
  struct Client *target_p;
  char buf[BUFSIZE];
  dlink_node *ptr;

  send_umode(NULL, source_p, old, SEND_UMODES, buf);

  DLINK_FOREACH(ptr, serv_list.head)
  {
    target_p = ptr->data;

    if ((target_p != client_p) && (target_p != source_p) && (*buf))
    {
      if ((!(ServerInfo.hub && IsCapable(target_p, CAP_LL))) ||
          (target_p->localClient->serverMask &
           source_p->lazyLinkClientExists))
        sendto_one(target_p, ":%s MODE %s :%s",
                   ID_or_name(source_p, target_p), ID_or_name(source_p, target_p), buf);
    }
  }

  if (client_p && MyClient(client_p))
    send_umode(client_p, source_p, old, ALL_UMODES, buf);
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
             me.name, source_p->name, me.name, ircd_version);

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
        sendto_realops_flags(UMODE_ADMIN, L_ALL,
                             "X-line Rejecting [%s] [%s], user %s [%s]",
                             source_p->info, reason,
                             get_client_name(client_p, HIDE_IP),
                             source_p->localClient->sockhost);
      }

      ServerStats->is_ref++;      
      exit_client(client_p, source_p, &me, "Bad user info");
      return(CLIENT_EXITED);
    }
    else
      sendto_realops_flags(UMODE_ADMIN, L_ALL,
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
  unsigned int old = (source_p->umodes & ALL_UMODES);
  struct AccessItem *oconf;

  SetOper(source_p);

  if (ConfigFileEntry.oper_umodes)
    source_p->umodes |= ConfigFileEntry.oper_umodes & ALL_UMODES;
  else
    source_p->umodes |= (UMODE_SERVNOTICE|UMODE_OPERWALL|
			 UMODE_WALLOP|UMODE_LOCOPS) & ALL_UMODES;

  Count.oper++;

  assert(dlinkFind(&oper_list, source_p) == NULL);
  dlinkAdd(source_p, make_dlink_node(), &oper_list);

  assert(source_p->localClient->confs.head);
  oconf = map_to_conf((source_p->localClient->confs.head)->data);

  SetOFlag(source_p, oconf->port);

  if (IsOperAdmin(source_p) || IsOperHiddenAdmin(source_p))
    source_p->umodes |= UMODE_ADMIN;
  if (!IsOperN(source_p))
    source_p->umodes &= ~UMODE_NCHANGE;

  if (source_p->localClient->operflags & OPER_FLAG_NETADMIN)
    source_p->umodes |= UMODE_NETADMIN;

  if (source_p->localClient->operflags & OPER_FLAG_TECHADMIN)
    source_p->umodes |= UMODE_TECHADMIN;  

  source_p->umodes |= UMODE_HELPOP;

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

    source_p->flags |= FLAGS_USERCLOAK;
    source_p->umodes |= UMODE_CLOAK;
  }

  sendto_realops_flags(UMODE_ALL, L_ALL, "%s (%s@%s) is now an operator",
                       source_p->name, source_p->username, GET_CLIENT_HOST(source_p));
  send_umode_out(source_p, source_p, old);
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
