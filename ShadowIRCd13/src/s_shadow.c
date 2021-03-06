#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <utmp.h>
#include "h.h"


#define AllocCpy(x,y) x = (char *) MyMalloc(strlen(y) + 1); strcpy(x,y)

aCRline *crlines = NULL;

char *cannotjoin_msg = NULL;

void cr_report (aClient *);

/*
** m_sethost
**
** Based on m_sethost by Stskeeps
*/
int
m_sethost (aClient * client_p, aClient * source_p, int parc, char *parv[])
{
  aClient *target_p;
  char *p;
  int bad_dns, dots;

  if (check_registered (source_p))
    return 0;

  /*
   *  Getting rid of the goto's - ShadowMaster
   */
  if (!IsPrivileged (source_p))
    {
      sendto_one (source_p, err_str (ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }


  if (parc < 3 && IsPerson (source_p))
    {
      if (MyConnect (source_p))
	{
	  sendto_one (source_p,
		      ":%s NOTICE %s :*** [SETHOST] -- Syntax: /SETHOST <nickname> <hostname>",
		      me.name, parv[0]);
	}
      return 0;
    }

  /* Safety net. */
  if (parc < 3)
    {
      return 0;
    }

  /* paranoia */
  if (parv[2] == NULL)
    {
      return 0;
    }

  if (strlen (parv[2]) > HOSTLEN)
    {
      if (MyConnect (source_p))
	{
	  sendto_one (source_p,
		      ":%s NOTICE %s :*** [SETHOST] -- Error: Hostnames are limited to %d characters.",
		      me.name, source_p->name, HOSTLEN);
	}
      return 0;
    }
  /*
   * We would want this to be at least 4 characters long.  - ShadowMaster
   */
  if (strlen (parv[2]) < 4)
    {
      if (MyConnect (source_p))
	{
	  sendto_one (source_p,
		      ":%s NOTICE %s :*** [SETHOST] -- Error: Please enter a valid hostname.",
		      me.name, source_p->name);
	}
      return 0;
    }

  dots = 0;
  p = parv[2];
  bad_dns = NO;

#ifdef INET6
  if (*p == ':')		/* We dont accept hostnames with ':' */
    {
      sendto_one (source_p,
		  ":%s NOTICE %s :*** [SETHOST] -- Error: First char in you hostname is ':'",
		  me.name, parv[0]);
      return 0;
    }
#endif

  while (*p)
    {
      if (!isalnum (*p))
	{
#ifndef INET6
	  if ((*p != '-') && (*p != '.'))
#else
	  if ((*p != '-') && (*p != '.') && (*p != ':'))
#endif
	    bad_dns = YES;
	}
#ifndef INET6
      if (*p == '.')
#else
      if ((*p == '.') || (*p == ':'))
#endif
	dots++;

      p++;
    }

  if (bad_dns == YES)
    {
      if (MyConnect (source_p))
	{
	  sendto_one (source_p,
		      ":%s NOTICE %s :*** [SETHOST] -- Error: A hostname may contain a-z, A-Z, 0-9, '-' & '.'",
		      me.name, parv[0]);
	}
      return 0;
    }
  if (!dots)
    {
      if (MyConnect (source_p))
	{
	  sendto_one (source_p,
		      ":%s NOTICE %s :*** [SETHOST] -- Error: You must have at least one . (dot) in the hostname.",
		      me.name, parv[0]);
	}
      return 0;
    }


  if ((target_p = find_person (parv[1], NULL)))
    {
      if (!IsHidden (target_p))
	{
	  SetHidden (target_p);
	}
      /*
       * Now using strncpyzt instead of ircsprintf - ShadowMaster
       */
      strncpyzt (target_p->user->virthost, parv[2], HOSTLEN);

      sendto_serv_butone (client_p, ":%s SETHOST %s %s", me.name,
			  target_p->name, parv[2]);

      if (MyClient (source_p))
	{
	  if (source_p == target_p)
	    {
	      sendto_snomask
		(SNOMASK_NETINFO, "Hmmm, %s (%s@%s) is sethosting as [%s]...",
		 me.name, parv[0], source_p->user->username,
		 source_p->user->host, source_p->user->virthost);
	      sendto_one (source_p,
			  ":%s NOTICE %s :*** [SETHOST] -- Your virtual hostname is now \2%s\2",
			  me.name, parv[0], source_p->user->virthost);
	    }

	  else
	    {
	      sendto_snomask
		(SNOMASK_NETINFO, "Hmmm, %s (%s@%S) is sethosting [%s] as [%s]...",
		 me.name, parv[0], target_p->name, target_p->user->username,
		 target_p->user->host, target_p->user->virthost);
	      sendto_one (source_p,
			  ":%s NOTICE %s :*** [SETHOST] -- Set the virtualhost of %s to \2%s\2",
			  me.name, parv[0], target_p->name,
			  target_p->user->virthost);

	    }
	}
    }
  else
    {
      sendto_one (source_p, err_str (ERR_NOSUCHNICK), me.name, source_p->name,
		  parv[1]);
      return 0;
    }
  return 0;
}

int
m_setident (aClient * client_p, aClient * source_p, int parc, char *parv[])
{
  aClient *target_p;

  if (check_registered (source_p))
    return 0;

  /*
   *  Getting rid of the goto's - ShadowMaster
   */
  if (!IsPrivileged (source_p))
    {
      sendto_one (source_p, err_str (ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }


  if (parc < 3 && IsPerson (source_p))
    {
      if (MyConnect (source_p))
        {
          sendto_one (source_p,
                      ":%s NOTICE %s :*** [SETIDENT] -- Syntax: /SETIDENT <nickname> <hostname>",
                      me.name, parv[0]);
        }
      return 0;
    }

  /* Safety net. */
  if (parc < 3)
    {
      return 0;
    }

  /* paranoia */
  if (parv[2] == NULL)
    {
      return 0;
    }
  if (strlen (parv[2]) > USERLEN)
    {
      if (MyConnect (source_p))
        {
          sendto_one (source_p,
                      ":%s NOTICE %s :*** [SETIDENT] -- Error: Idents are limited to %d characters.",
                      me.name, source_p->name, USERLEN);
        }
      return 0;
    }

  if ((target_p = find_person (parv[1], NULL)))
    {
      strncpyzt (target_p->user->username, parv[2], USERLEN);

      sendto_serv_butone (client_p, ":%s SETIDENT %s %s", me.name,
                          target_p->name, parv[2]);

      if (MyClient (source_p))
        {
          if (source_p == target_p)
            {
              sendto_snomask
                (SNOMASK_NETINFO, "Hmmm, %s (%s@%s) set their ident to [%s]...",
                 me.name, parv[0], source_p->user->username,
                 source_p->user->host, source_p->user->username);
              sendto_one (source_p,
                          ":%s NOTICE %s :*** [SETIDENT] -- Your virtual ident is now \2%s\2",
                          me.name, parv[0], source_p->user->username);
            }

          else
            {
              sendto_snomask
                (SNOMASK_NETINFO, "Hmmm, %s set the ident of %s (%s@%s) to [%s]...",
                 me.name, parv[0], target_p->name, target_p->user->username,
                 target_p->user->host, target_p->user->username);
              sendto_one (source_p,
                          ":%s NOTICE %s :*** [SETIDENT] -- Set the ident of %s to \2%s\2",
                          me.name, parv[0], target_p->name,
                          target_p->user->virthost);

            }
        }
    }
  else
    {
      sendto_one (source_p, err_str (ERR_NOSUCHNICK), me.name, source_p->name,
                  parv[1]);
      return 0;
    }
  return 0;
}

/*
** m_vhost
**
** Based on the original m_oper in bahamut.
** Allows regular users to use VHOST's setup for them by
** the server admin. - ShadowMaster
**
** parv[0] = sender prefix
** parv[1] = username
** parv[2] = password
*/
int
m_vhost (aClient * client_p, aClient * source_p, int parc, char *parv[])
{
  aConfItem *aconf;
  char *name, *password, *encr;

#define CRYPT_VHOST_PASSWORD
#ifdef CRYPT_VHOST_PASSWORD
  extern char *crypt ();

#endif

  name = parc > 1 ? parv[1] : (char *) NULL;
  password = parc > 2 ? parv[2] : (char *) NULL;

  if (!IsServer (client_p) && (BadPtr (name) || BadPtr (password)))
    {
      sendto_one (source_p, err_str (ERR_NEEDMOREPARAMS),
		  me.name, parv[0], "VHOST");
      return 0;
    }
  if (!(aconf = find_conf_name (name, CONF_VHOST)))
    {
      sendto_one (source_p,
		  ":%s NOTICE %s :*** [VHOST] -- No V line for that username.",
		  me.name, parv[0]);

      return 0;
    }

#ifdef CRYPT_VHOST_PASSWORD
  /* use first two chars of the password they send in as salt */
  /* passwd may be NULL pointer. Head it off at the pass... */
  if (password && *aconf->passwd)
    encr = crypt (password, aconf->passwd);
  else
    encr = "";
#else
  encr = password;
#endif /* CRYPT_VHOST_PASSWORD */

  if ((aconf->status & CONF_VHOST) &&
      StrEq (encr, aconf->passwd) && !attach_conf (source_p, aconf))
    {
      int old = (source_p->umode & ALL_UMODES);

      /*
       * In theory we can have very long hostnames. But we wanna stay securely on the safe side of
       * things. Prevent admins from goofing up and reject the /VHOST if its too long then warn the user
       * and the opers about it - ShadowMaster
       */

      if (strlen (aconf->host) > HOSTLEN)
	{
	  sendto_one (source_p,
		      ":%s NOTICE %s :*** [VHOST] -- The hostname set in the vhost.conf for your login is too long. Please contact the admin to have this corrected.",
		      me.name, parv[0]);
	  return 0;
	}

      if (!IsHidden (source_p))
	{
	  SetHidden (source_p);
	}

      strcpy (source_p->user->virthost, aconf->host);
      sendto_serv_butone (client_p, ":%s SETHOST %s %s", me.name, parv[0],
			  source_p->user->virthost);

      sendto_one (source_p,
		  ":%s NOTICE %s :*** [VHOST] -- Your virtual hostname is now \2%s\2",
		  me.name, parv[0], source_p->user->virthost);

      send_umode_out (client_p, source_p, old);




    }
  else
    {
      (void) detach_conf (source_p, aconf);
      sendto_one (source_p, err_str (ERR_PASSWDMISMATCH), me.name, parv[0]);
    }
  return 0;
}

/*
**
** check_clones by Crash
**
**	Slight modifications for UltimateIRCd by ShadowMaster.
**
**	Checks the ENTIRE network for clients with the same host,
**	counts them and returns 0 if the clone count is below MAXCLONES
**	and -1 if the clone count is above MAXCLONES.
**	Ignores ULined clients.
**
*/
int
check_clones (aClient * client_p, aClient * source_p)
{
  aClient *target_p;
  int c = 1;
  for (target_p = client; target_p; target_p = target_p->next)
    {
      /* first, are they a "Real" person? */
      if (!target_p->user)
	continue;
      /* lets let services clone ;-) */
      if (IsULine (target_p))
	continue;
      /* just ignore servers */
      if (IsServer (target_p))
	continue;
      /* dont count the client doing it */
      if (irccmp (source_p->name, target_p->name) == 0)
	continue;
      /* If the network wants its opers to be allowed to clone.... - ShadowMaster */
      if ((OPER_CLONE_LIMIT_BYPASS == 1) && IsAnOper (target_p))
	continue;
      /* ahh! the hosts match!  count it */
      if (irccmp (source_p->user->host, target_p->user->host) == 0)
	c++;
    }

  /* WHAT?! exceding clone limit!? KILL EM?! */

  if ((CLONE_LIMIT - c) < 0)
    {
      return -1;
    }
  return 0;
}


/* Star Channel Restric */


/*
** channel_canjoin from UnrealIRCd
*/
int
channel_canjoin (aClient * source_p, char *name)
{
  aCRline *p;
  if (IsAnOper (source_p))
    return 1;
  if (IsULine (source_p))
    return 1;
  if (!crlines)
    return 1;
  for (p = crlines; p; p = p->next)
    {
      if (!match (p->channel, name))
	return 1;
    }
  return 0;
}

/*
** cr_add from UnrealIRCd
*/
int
cr_add (char *channel, int type)
{
  aCRline *fl;
  fl = (aCRline *) MyMalloc (sizeof (aCRline));
  AllocCpy (fl->channel, channel);
  fl->type = type;
  fl->next = crlines;
  fl->prev = NULL;
  if (crlines)
    crlines->prev = fl;
  crlines = fl;
  return 0;
}

aCRline *
cr_del (fl)
     aCRline *fl;
{
  aCRline *p, *q;
  for (p = crlines; p; p = p->next)
    {
      if (p == fl)
	{
	  q = p->next;
	  MyFree ((char *) p->channel);
	  /* chain1 to chain3 */
	  if (p->prev)
	    {
	      p->prev->next = p->next;
	    }
	  else
	    {
	      crlines = p->next;
	    }
	  if (p->next)
	    {
	      p->next->prev = p->prev;
	    }
	  MyFree ((aCRline *) p);
	  return q;
	}
    }
  return NULL;
}


/*
** cr_loadconf from UnrealIRCd
*/
int
cr_loadconf (char *filename, int type)
{
  char buf[2048];
  char *x, *y;
  FILE *f;
  f = fopen (filename, "r");
  if (!f)
    return -1;
  while (fgets (buf, 2048, f))
    {
      if (buf[0] == '#' || buf[0] == '/' || buf[0] == '\0'
	  || buf[0] == '\n' || buf[0] == '\r')
	continue;
      iCstrip (buf);
      x = strtok (buf, " ");
      if (irccmp (x, "allow") == 0)
	{
	  y = strtok (NULL, " ");
	  if (!y)
	    continue;
	  cr_add (y, 0);
	}
      else if (irccmp (x, "msg") == 0)
	{
	  y = strtok (NULL, "");
	  if (!y)
	    continue;
	  if (cannotjoin_msg)
	    MyFree ((char *) cannotjoin_msg);
	  cannotjoin_msg = MyMalloc (strlen (y) + 1);
	  strcpy (cannotjoin_msg, y);
	}
    }
  return 0;
}

/*
** cr_rehash
*/
void
cr_rehash (void)
{
  aCRline *p, q;
  for (p = crlines; p; p = p->next)
    {
      if ((p->type == 0) || (p->type == 2))
	{
	  q.next = cr_del (p);
	  p = &q;
	}
    }
  cr_loadconf (IRCD_RESTRICT, 1);
}

/*
** cr_report from UnrealIRCd
**
** Adapted for UltimateIRCd by ShadowMaster
*/
void
cr_report (aClient * source_p)
{
  aCRline *tmp;
  char *filemask;
  if (!crlines)
    {
      sendto_one (source_p,
		  ":%s %d %s :There are currently no channel restrictions in place on %s",
		  me.name, RPL_SETTINGS, source_p->name, me.name);
      sendto_one (source_p, rpl_str (RPL_ENDOFSETTINGS), me.name,
		  source_p->name);
      return;
    }

  sendto_one (source_p,
	      ":%s %d %s :\2坍之才~之才~之才~之才� Allowed Channels 坍之才~之才~之才~之才吭2",
	      me.name, RPL_SETTINGS, source_p->name);
  sendto_one (source_p, ":%s %d %s :", me.name, RPL_SETTINGS, source_p->name);
  for (tmp = crlines; tmp; tmp = tmp->next)
    {
      filemask = BadPtr (tmp->channel) ? "<NULL>" : tmp->channel;
      sendto_one (source_p, ":%s %d %s :%s", me.name,
		  RPL_SETTINGS, source_p->name, filemask);
    }
  sendto_one (source_p, ":%s %d %s :", me.name, RPL_SETTINGS, source_p->name);
  sendto_one (source_p,
	      ":%s %d %s :\2坍之才~之才~之才~之才~之才~之才~之才~之才~之才~之才~之才~之才~之才吭2",
	      me.name, RPL_SETTINGS, source_p->name);
  sendto_one (source_p, rpl_str (RPL_ENDOFSETTINGS), me.name, source_p->name);
}

/* End Channel Restrict */


/*
** m_makepass based on the code by Nelson Minar (minar@reed.edu)
**
** Copyright (C) 1991, all rights reserved.
**
**	parv[0] = sender prefix
**	parv[1] = password to encrypt
*/
int
m_makepass (client_p, source_p, parc, parv)
     aClient *client_p, *source_p;
     int parc;
     char *parv[];
{
  static char saltChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
  char salt[3];
  extern char *crypt ();
  int useable = 0;
  if (parc > 1)
    {
      if (strlen (parv[1]) >= 1)
	useable = 1;
    }

  if (useable == 0)
    {
      sendto_one (source_p,
		  ":%s NOTICE %s :*** Encryption's MUST be atleast 1 character in length",
		  me.name, parv[0]);
      return 0;
    }
  srandom (time (0));
  salt[0] = saltChars[random () % 64];
  salt[1] = saltChars[random () % 64];
  salt[2] = 0;
  if ((strchr (saltChars, salt[0]) == NULL)
      || (strchr (saltChars, salt[1]) == NULL))
    {
      sendto_one (source_p, ":%s NOTICE %s :*** Illegal salt %s",
		  me.name, parv[0], salt);
      return 0;
    }


  sendto_one (source_p,
	      ":%s NOTICE %s :*** Encryption for [%s] is %s",
	      me.name, parv[0], parv[1], crypt (parv[1], salt));
  return 0;
}
