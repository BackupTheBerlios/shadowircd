/*
 *  shadowircd: an advanced Internet Relay Chat Daemon(ircd).
 *  listener.c: Listens on a port.
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
 *  $Id: listener.c,v 1.7 2004/02/05 20:15:48 nenolod Exp $
 */

#include "stdinc.h"
#include "listener.h"
#include "client.h"
#include "fdlist.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "s_bsd.h"
#include "irc_getnameinfo.h"
#include "irc_getaddrinfo.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_stats.h"
#include "send.h"
#include "memory.h"
#include "tools.h"


static PF accept_connection;

static dlink_list ListenerPollList = { NULL, NULL, 0 };
static void close_listener(struct Listener *listener);

static struct Listener *
make_listener(int port, struct irc_ssaddr *addr)
{
  struct Listener *listener =
    (struct Listener *)MyMalloc(sizeof(struct Listener));
  assert(listener != 0);

  listener->name = me.name;
  listener->port = port;
  listener->fd   = -1;
  memcpy(&listener->addr, addr, sizeof(struct irc_ssaddr));

  return(listener);
}

void
free_listener(struct Listener *listener)
{
  assert(listener != NULL);

  if (listener == NULL)
    return;

  dlinkDelete(&listener->listener_node, &ListenerPollList);
  MyFree(listener);
}

/*
 * get_listener_name - return displayable listener name and port
 * returns "host.foo.org:6667" for a given listener
 */
const char *
get_listener_name(const struct Listener* listener)
{
  static char buf[HOSTLEN + HOSTLEN + PORTNAMELEN + 4];

  assert(listener != NULL);

  if (listener == NULL)
    return(NULL);

  ircsprintf(buf, "%s[%s/%u]",
             me.name, listener->name, listener->port);
  return(buf);
}

/* show_ports()
 *
 * inputs       - pointer to client to show ports to
 * output       - none
 * side effects - send port listing to a client
 */
void 
show_ports(struct Client *source_p)
{
  dlink_node *ptr;
  struct Listener *listener;

  DLINK_FOREACH(ptr, ListenerPollList.head)
  {
    listener = ptr->data;
    sendto_one(source_p, form_str(RPL_STATSPLINE),
               me.name, source_p->name,
               listener->is_ssl ? 'S' : 'P', listener->port,
               IsAdmin(source_p) ? listener->name : me.name,
               listener->ref_count,
               (listener->active)?"active":"disabled");
  }
}

/*
 * inetport - create a listener socket in the AF_INET or AF_INET6 domain,
 * bind it to the port given in 'port' and listen to it
 * returns true (1) if successful false (0) on error.
 *
 * If the operating system has a define for SOMAXCONN, use it, otherwise
 * use HYBRID_SOMAXCONN
 */
#ifdef SOMAXCONN
#undef HYBRID_SOMAXCONN
#define HYBRID_SOMAXCONN SOMAXCONN
#endif

static int 
inetport(struct Listener *listener)
{
  struct irc_ssaddr lsin;
  int fd;
  int opt = 1;

  /*
   * At first, open a new socket
   */
  fd = comm_open(listener->addr.ss.ss_family, SOCK_STREAM, 0, "Listener socket");
  memset(&lsin, 0, sizeof(lsin));
  memcpy(&lsin, &listener->addr, sizeof(struct irc_ssaddr));
  
  irc_getnameinfo((struct sockaddr*)&lsin, lsin.ss_len, listener->vhost, 
        HOSTLEN, NULL, 0, NI_NUMERICHOST);
  listener->name = listener->vhost;

  if (fd == -1)
  {
    report_error(L_ALL, "opening listener socket %s:%s",
                 get_listener_name(listener), errno);
    return(0);
  }
  else if ((HARD_FDLIMIT - 10) < fd)
  {
    report_error(L_ALL, "no more connections left for listener %s:%s",
                 get_listener_name(listener), errno);
    fd_close(fd);
    return(0);
  }
  /*
   * XXX - we don't want to do all this crap for a listener
   * set_sock_opts(listener);
   */
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*) &opt, sizeof(opt)))
  {
    report_error(L_ALL, "setting SO_REUSEADDR for listener %s:%s",
                 get_listener_name(listener), errno);
    fd_close(fd);
    return(0);
  }

  /*
   * Bind a port to listen for new connections if port is non-null,
   * else assume it is already open and try get something from it.
   */
  lsin.ss_port = htons(listener->port);


  if (bind(fd, (struct sockaddr*)&lsin,
        lsin.ss_len))
  {
    report_error(L_ALL, "binding listener socket %s:%s",
                 get_listener_name(listener), errno);
    fd_close(fd);
    return(0);
  }

  if (listen(fd, HYBRID_SOMAXCONN))
  {
    report_error(L_ALL, "listen failed for %s:%s",
                 get_listener_name(listener), errno);
    fd_close(fd);
    return(0);
  }

  /*
   * XXX - this should always work, performance will suck if it doesn't
   */
  if (!set_non_blocking(fd))
    report_error(L_ALL, NONB_ERROR_MSG, get_listener_name(listener), errno);

  listener->fd = fd;

  /* Listen completion events are READ events .. */

  accept_connection(fd, listener);
  return(1);
}

static struct Listener *
find_listener(int port, struct irc_ssaddr *addr)
{
  dlink_node *ptr;
  struct Listener *listener    = NULL;
  struct Listener *last_closed = NULL;

  DLINK_FOREACH(ptr, ListenerPollList.head)
  {
    listener = ptr->data;

    if ((port == listener->port) &&
        (!memcmp(addr, &listener->addr, sizeof(struct irc_ssaddr))))
    {
      /* Try to return an open listener, otherwise reuse a closed one */
      if (listener->fd == -1)
        last_closed = listener;
      else
        return(listener);
    }
  }

  return(last_closed);
}

/*
 * add_listener- create a new listener
 * port - the port number to listen on
 * vhost_ip - if non-null must contain a valid IP address string in
 * the format "255.255.255.255"
 */
void 
add_listener(int port, const char* vhost_ip, int is_ssl)
{
  struct Listener *listener;
  struct irc_ssaddr vaddr;
  struct addrinfo hints, *res;
  char portname[PORTNAMELEN + 1];
#ifdef IPV6
  static short int pass = 0; /* if ipv6 and no address specified we need to
				have two listeners; one for each protocol. */
#endif

  /*
   * if no port in conf line, don't bother
   */
  if (port == 0)
    return;

  memset(&vaddr, 0, sizeof(vaddr));

  /* Set up the hints structure */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  /* Get us ready for a bind() and don't bother doing dns lookup */
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

#ifdef IPV6
  if (ServerInfo.can_use_v6)
  {
    snprintf(portname, PORTNAMELEN, "%d", port);
    irc_getaddrinfo("::", portname, &hints, &res);
    vaddr.ss.ss_family = AF_INET6;
    assert(res != NULL);

    memcpy((struct sockaddr*)&vaddr, res->ai_addr, res->ai_addrlen);
    vaddr.ss_port = port;
    vaddr.ss_len = res->ai_addrlen;
    irc_freeaddrinfo(res);
  }
  else
#endif
  {
    struct sockaddr_in *v4 = (struct sockaddr_in*) &vaddr;
    v4->sin_addr.s_addr = INADDR_ANY;
    vaddr.ss.ss_family = AF_INET;
    vaddr.ss_len = sizeof(struct sockaddr_in);
    v4->sin_port = htons(port);
  }

  snprintf(portname, PORTNAMELEN, "%d", port);

  if (vhost_ip)
  {
    if (irc_getaddrinfo(vhost_ip, portname, &hints, &res))
        return;

    assert(res != NULL);

    memcpy((struct sockaddr*)&vaddr, res->ai_addr, res->ai_addrlen);
    vaddr.ss_port = port;
    vaddr.ss_len = res->ai_addrlen;
    irc_freeaddrinfo(res);
  }
#ifdef IPV6
  else if (pass == 0 && ServerInfo.can_use_v6)
  {
    /* add the ipv4 listener if we havent already */
    pass = 1;
    add_listener(port, "0.0.0.0", is_ssl);
  }
  pass = 0;
#endif

  if ((listener = find_listener(port, &vaddr)))
  {
    if (listener->fd > -1)
      return;
  }
  else
  {
    listener = make_listener(port, &vaddr);
    listener->is_ssl = is_ssl;
    dlinkAdd(listener, &listener->listener_node, &ListenerPollList);
  }

  listener->fd = -1;


  if (inetport(listener))
    listener->active = 1;
  else
    close_listener(listener);
}

/*
 * close_listener - close a single listener
 */
static void
close_listener(struct Listener *listener)
{
  assert(listener != NULL);

  if (listener == NULL)
    return;

  if (listener->fd >= 0)
  {
    fd_close(listener->fd);
    listener->fd = -1;
  }

  listener->active = 0;

  if (listener->ref_count)
    return;

  free_listener(listener);
}

/*
 * close_listeners - close and free all listeners that are not being used
 */
void 
close_listeners(void)
{
  dlink_node *ptr;
  dlink_node *next_ptr;
  struct Listener *listener;

  /* close all 'extra' listening ports we have
   */
  DLINK_FOREACH_SAFE(ptr, next_ptr, ListenerPollList.head)
  {
    listener = ptr->data;
    close_listener(listener);
  }
}

#define TOOFAST_WARNING "ERROR :Trying to reconnect too fast.\r\n"
#define DLINE_WARNING "ERROR :You have been D-lined.\r\n"

static void 
accept_connection(int pfd, void *data)
{
  static time_t last_oper_notice = 0;
  struct irc_ssaddr sai;
  struct irc_ssaddr addr;
  int fd;
  int pe;
  struct Listener *listener = data;

  memset(&sai, 0, sizeof(sai));
  memset(&addr, 0, sizeof(addr));

  assert(listener != NULL);
  if (listener == NULL)
    return;
  listener->last_accept = CurrentTime;

  /* There may be many reasons for error return, but
   * in otherwise correctly working environment the
   * probable cause is running out of file descriptors
   * (EMFILE, ENFILE or others?). The man pages for
   * accept don't seem to list these as possible,
   * although it's obvious that it may happen here.
   * Thus no specific errors are tested at this
   * point, just assume that connections cannot
   * be accepted until some old is closed first.
   */

  fd = comm_accept(listener->fd, &sai);

  if (fd < 0)
  {
    /* Re-register a new IO request for the next accept .. */
    comm_setselect(listener->fd, FDLIST_SERVICE, COMM_SELECT_READ,
                   accept_connection, listener, 0);
    return;
  }

  memcpy(&addr, &sai, sizeof(struct irc_ssaddr));

  /*
   * check for connection limit
   */
  if ((HARD_FDLIMIT - 10) < fd)
  {
    ++ServerStats->is_ref;
      /*
       * slow down the whining to opers bit
       */
      if((last_oper_notice + 20) <= CurrentTime)
	{
	  sendto_realops_flags(UMODE_ALL, L_ALL,"All connections in use. (%s)",
			       get_listener_name(listener));
	  last_oper_notice = CurrentTime;
	}
      send(fd, "ERROR :All connections in use\r\n", 32, 0);
      fd_close(fd);
      /* Re-register a new IO request for the next accept .. */
      comm_setselect(listener->fd, FDLIST_SERVICE, COMM_SELECT_READ,
                     accept_connection, listener, 0);
      return;
    }

  /* Do an initial check we aren't connecting too fast or with too many
   * from this IP... */
  if ((pe = conf_connect_allowed(&addr, sai.ss.ss_family)) != 0)
  {
   ServerStats->is_ref++;
   switch (pe)
   {
    case BANNED_CLIENT:
     send(fd, DLINE_WARNING, sizeof(DLINE_WARNING)-1, 0);
     break;
    case TOO_FAST:
     send(fd, TOOFAST_WARNING, sizeof(TOOFAST_WARNING)-1, 0);
     break;
   }
   fd_close(fd);
   /* Re-register a new IO request for the next accept .. */
   comm_setselect(listener->fd, FDLIST_SERVICE, COMM_SELECT_READ,
                  accept_connection, listener, 0);
   return;
  }
  ServerStats->is_ac++;

  add_connection(listener, fd);

  /* Re-register a new IO request for the next accept .. */
  comm_setselect(listener->fd, FDLIST_SERVICE, COMM_SELECT_READ,
    accept_connection, listener, 0);
}

