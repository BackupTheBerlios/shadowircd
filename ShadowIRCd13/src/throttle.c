/*
 *  UltimateIRCd - an Internet Relay Chat Daemon, src/throttle.c
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
 *  Refer to the documentation within doc/authors/ for full credits and copyrights.
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
 *  $Id: throttle.c,v 1.1 2003/11/04 07:11:32 nenolod Exp $
 *
 */

/*
 * Copyright 2000, 2001 Chip Norkus
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2a. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 * 2b. Redistribution in binary form requires specific prior written
 *     authorization of the maintainer.
 * 
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgement:
 *    This product includes software developed by Chip Norkus.
 * 
 * 4. The names of the maintainer, developers and contributors may not be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE MAINTAINER, DEVELOPERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE DEVELOPERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "res.h"
#include "h.h"
#include "numeric.h"
#include "blalloc.h"

#include <sys/types.h>
#include <sys/socket.h>

#include "queue.h"
#include "throttle.h"

BlockHeap *hashent_freelist;
BlockHeap *throttle_freelist;

extern void sendto_snomask(int, char *, ...);

/*******************************************************************************
 * hash code here.  why isn't it in hash.c?  see the license. :)
 ******************************************************************************/

SLIST_HEAD (hashent_list_t, hashent_t);
typedef struct hashent_list_t hashent_list;

typedef struct hashent_t
{
  void *ent;
    SLIST_ENTRY (hashent_t) lp;
}
hashent;

typedef struct hash_table_t
{
  int size;			/* this should probably always be prime :) */
  hashent_list *table;		/* our table */
  size_t keyoffset;		/* this stores the offset of the key from the
				   given structure */
  size_t keylen;		/* the length of the key. if 0, assume key
				   is a NULL terminated string */

#define HASH_FL_NOCASE 0x1	/* ignore case (tolower before hash) */
#define HASH_FL_STRING 0x2	/* key is a nul-terminated string, treat len
				   as a maximum length to hash */
  int flags;
  /* our comparison function, used in hash_find_ent().  this behaves much
   * like the the compare function is used in qsort().  This means that a
   * return of 0 (ZERO) means success! (this lets you use stuff like
   * strncmp easily) */
  int (*cmpfunc) (void *, void *);
}
hash_table;

/* this function creates a hashtable with 'elems' buckets (elems should be
* prime for best efficiency).  'offset' is the offset of the key from
* structures being added (this should be obtained with the 'offsetof()'
* function).  len is the length of the key, and flags are any flags for the
* table (see above).  cmpfunc is the function which should be used for
* comparison when calling 'hash_find' */
hash_table *create_hash_table (int elems, size_t offset, size_t len,
			       int flags, int (*cmpfunc) (void *, void *));
/* this function destroys a previously created hashtable */
void destroy_hash_table (hash_table * table);
/* this function resizes a hash-table to the new size given with 'elems'.
* this is not in any way inexpensive, and should really not be done very
* often.  */
void resize_hash_table (hash_table * table, int elems);
/* this function gets the hash value of a given key, relative to the size of
* the hashtable */
unsigned int hash_get_key_hash (hash_table * table, void *key, size_t offset);
/* these functions do what you would expect, adding/deleting/finding items
* in a hash table */
int hash_insert (hash_table * table, void *ent);
int hash_delete (hash_table * table, void *ent);
void *hash_find (hash_table * table, void *key);

hashent *
hashent_alloc ()
{
  return BlockHeapALLOC (hashent_freelist, hashent);
}

void
hashent_free (hashent * hp)
{
  BlockHeapFree (hashent_freelist, hp);
}

/* hash_table creation function.  given the user's paramters, allocate
 * and empty a new hash table and return it. */
hash_table *
create_hash_table (int elems, size_t offset, size_t len,
		   int flags, int (*cmpfunc) (void *, void *))
{
  hash_table *htp = malloc (sizeof (hash_table));

  htp->size = elems;
  htp->keyoffset = offset;
  htp->keylen = len;
  htp->flags = flags;
  htp->cmpfunc = cmpfunc;

  htp->table = malloc (sizeof (hashent_list) * htp->size);
  memset (htp->table, 0, sizeof (hashent_list) * htp->size);

  return htp;
}

/* hash_table destroyer.  sweep through the given table and kill off every
 * hashent */
void
destroy_hash_table (hash_table * table)
{
  hashent *hep;
  int i;

  for (i = 0; i < table->size; i++)
    {
      while (!SLIST_EMPTY (&table->table[i]))
	{
	  hep = SLIST_FIRST (&table->table[i]);
	  SLIST_REMOVE_HEAD (&table->table[i], lp);
	  hashent_free (hep);
	}
    }
  free (table->table);
  free (table);
}

/* this is an expensive function.  it's not the sort of thing one should be
 * calling a lot, however, in the right situations it can provide a lot of
 * benefit */
void
resize_hash_table (hash_table * table, int elems)
{
  hashent_list *oldtable;
  int oldsize, i;
  hashent *hep;

  /* preserve the old table, then create a new one.  */
  oldtable = table->table;
  oldsize = table->size;
  table->size = elems;
  table->table = malloc (sizeof (hashent_list) * table->size);
  memset (table->table, 0, sizeof (hashent_list) * table->size);

  /* now walk each bucket in the old table, pulling off individual entries
   * and re-adding them to the table as we go */
  for (i = 0; i < oldsize; i++)
    {
      while (!SLIST_EMPTY (&oldtable[i]))
	{
	  hep = SLIST_FIRST (&oldtable[i]);
	  hash_insert (table, hep->ent);
	  SLIST_REMOVE_HEAD (&oldtable[i], lp);
	  hashent_free (hep);
	}
    }
  free (oldtable);
}

/* get the hash of a given key.  really only useful for insert/delete */
unsigned int
hash_get_key_hash (hash_table * table, void *key, size_t offset)
{
  char *rkey = (char *) key + offset;
  int len = table->keylen;
  unsigned int hash = 0;

  if (!len)
    len = strlen (rkey);
  else if (table->flags & HASH_FL_STRING)
    {
      len = strlen (rkey);
      if (len > table->keylen)
	len = table->keylen;
    }
  /* I borrowed this algorithm from perl5.  Kudos to Larry Wall & co. */
  if (table->flags & HASH_FL_NOCASE)
    while (len--)
      hash = hash * 33 + tolower (*rkey++);
  else
    while (len--)
      hash = hash * 33 + *rkey++;

  return hash % table->size;
}

/* add the given item onto the hash */
int
hash_insert (hash_table * table, void *ent)
{
  int hash = hash_get_key_hash (table, ent, table->keyoffset);
  hashent *hep = hashent_alloc ();

  hep->ent = ent;
  SLIST_INSERT_HEAD (&table->table[hash], hep, lp);

  return 1;
}

/* delete the given item from the hash */
int
hash_delete (hash_table * table, void *ent)
{
  int hash = hash_get_key_hash (table, ent, table->keyoffset);
  hashent *hep;

  SLIST_FOREACH (hep, &table->table[hash], lp)
  {
    if (hep->ent == ent)
      break;
  }
  if (hep == NULL)
    return 0;
  SLIST_REMOVE (&table->table[hash], hep, hashent_t, lp);
  hashent_free (hep);
  return 1;
}

/* last, but not least, the find function.  given the table and the key to
 * look for, it hashes the key, and then calls the compare function in the
 * given table slice until it finds the item, or reaches the end of the
 * list. */
void *
hash_find (hash_table * table, void *key)
{
  int hash = hash_get_key_hash (table, key, 0);
  hashent *hep;

  SLIST_FOREACH (hep, &table->table[hash], lp)
  {
    if (!table->cmpfunc (&((char *) hep->ent)[table->keyoffset], key))
      return hep->ent;
  }

  return NULL;			/* not found */
}

/*******************************************************************************
 * actual throttle code here ;)
 ******************************************************************************/

LIST_HEAD (throttle_list_t, throttle_t) throttles;

     typedef struct throttle_t
     {
       char addr[HOSTIPLEN + 1];	/* address of the throttle */
       int conns;		/* number of connections seen from this
				   address. */
       time_t first;		/* first time we saw this IP in this stage */
       time_t last;		/* last time we saw this IP */
       time_t zline_start;	/* time we placed a zline for this host,
				   or 0 if no zline */
       int stage;		/* how many times this host has been z-lined */
       int re_zlines;		/* just a statistic -- how many times has this
				   host reconnected and had their ban reset */

         LIST_ENTRY (throttle_t) lp;
     }
throttle;

/* variables for the throttler */
     hash_table *throttle_hash;
     int throttle_tcount = THROTTLE_TRIGCOUNT;
     int throttle_ttime = THROTTLE_TRIGTIME;
     int throttle_rtime = THROTTLE_RECORDTIME;

#ifdef THROTTLE_ENABLE
     int throttle_enable = 1;
#else
     int throttle_enable = 0;
#endif

     int numthrottles = 0;	/* number of throttles in existence */

#ifdef THROTTLE_ENABLE
     void throttle_init (void)
{
  hashent_freelist = BlockHeapCreate (sizeof (hashent), 1024);
  throttle_freelist = BlockHeapCreate (sizeof (throttle), 1024);
  /* create the throttle hash. */
  throttle_hash = create_hash_table (THROTTLE_HASHSIZE,
				     offsetof (throttle, addr), HOSTIPLEN,
				     HASH_FL_STRING,
				     (int (*)(void *, void *)) strcmp);
}

throttle *
throttle_alloc ()
{
  return BlockHeapALLOC (throttle_freelist, throttle);
}

void
throttle_free (throttle * tp)
{
  BlockHeapFree (throttle_freelist, tp);
}

/* returns the zline time, in seconds */
static int
throttle_get_zline_time (int stage)
{
  switch (stage)
    {
    case -1:
      return 0;			/* no throttle */

    case 0:
      return 120;		/* 2 minutes */

    case 1:
      return 300;		/* 5 minutes */

    case 2:
      return 900;		/* 15 minutes */

    case 3:
      return 1800;		/* a half hour */

    default:
      return 3600;		/* an hour */
    }

  return 0;			/* dumb compiler */
}

void
throttle_remove (char *host)
{
  throttle *tp = hash_find (throttle_hash, host);

  if (tp)
    {
      LIST_REMOVE (tp, lp);
      hash_delete (throttle_hash, tp);
      throttle_free (tp);
      numthrottles--;
    }
}

void
throttle_force (char *host)
{
  throttle *tp = hash_find (throttle_hash, host);

  if (tp == NULL)
    {
      /* we haven't seen this one before, create a new throttle and add it to
       * the hash.  XXX: blockheap code should be used, but the blockheap
       * allocator available in ircd is broken beyond repair as far as I'm
       * concerned. -wd */
      tp = throttle_alloc ();;
      strcpy (tp->addr, host);

      tp->stage = -1;		/* no zline stage yet */
      tp->zline_start = 0;
      tp->conns = 0;
      tp->first = NOW;
      tp->re_zlines = 0;

      hash_insert (throttle_hash, tp);
      LIST_INSERT_HEAD (&throttles, tp, lp);
      numthrottles++;
    }

  /* now force them to be autothrottled if they reconnect. */
  tp->conns = -1;
  tp->last = tp->first = NOW;
}

/* fd is -1 for remote signons */
int
throttle_check (char *host, int fd, time_t sotime)
{
  throttle *tp = hash_find (throttle_hash, host);

  if (!throttle_enable)
    return 1;			/* always successful */

  /* If this is an old remote signon, just ignore it */
  if (fd == -1 && (NOW - sotime > throttle_ttime))
    return 1;

  /* E and F Lines exempt from throttle checks */
  if (find_matching_conf (&EList1, host))
    {
      return 1;
    }

  /* If this user is signing on 'in the future', we need to 
     fix that. Someone has a bad remote TS, perhaps we should complain */
  if (sotime > NOW)
    sotime = NOW;

  if (tp == NULL)
    {
      /* we haven't seen this one before, create a new throttle and add it to
       * the hash.  XXX: blockheap code should be used, but the blockheap
       * allocator available in ircd is broken beyond repair as far as I'm
       * concerned. -wd */
      tp = throttle_alloc ();;
      strcpy (tp->addr, host);

      tp->stage = -1;		/* no zline stage yet */
      tp->zline_start = 0;
      tp->conns = 0;
      tp->first = sotime;
      tp->re_zlines = 0;

      hash_insert (throttle_hash, tp);
      LIST_INSERT_HEAD (&throttles, tp, lp);
      numthrottles++;
    }
  else if (tp->zline_start)
    {
      time_t zlength = throttle_get_zline_time (tp->stage);

      /* If they're zlined, drop them */
      /* Also, reset the zline counter */
      if (sotime - tp->zline_start < zlength)
	{
	  /* don't reset throttle time for new remote signons */
	  if (fd == -1)
	    return 0;
	  /* 
	   * Reset the z-line period to start now
	   * Mean, but should get the bots and help the humans
	   */
	  tp->re_zlines++;
	  tp->zline_start = sotime;
	  return 0;
	}

      /* may look redundant, but it fixes it if 
         someone sets throttle_ttime to something insane */
      tp->conns = 0;
      tp->first = sotime;
      tp->zline_start = 0;
    }

  /* got a throttle, up the conns */
  if (tp->conns >= 0)
    tp->conns++;
  tp->last = sotime;

  /* check the time bits, if they exceeded the throttle timeout, we should
   * actually remove this structure from the hash and free it and create a
   * new one, except that would be preposterously expensive, so we just
   * re-set variables ;) -wd */
  if (sotime - tp->first > throttle_ttime)
    {
      tp->conns = 1;
      tp->first = sotime;

      /* we can probably gaurantee they aren't going to be throttled, return
       * success */
      return 1;
    }

  if (tp->conns == -1)
    {
      /* This is a forced throttle, drop 'em! */
      return 0;
    }

  if (tp->conns >= throttle_tcount)
    {
      /* mark them as z:lined (we do not actually add a Z:line as this would
       * be wasteful) and let local +c ops know about this */
      if (fd != -1)
	{
	  char errbufr[512];
	  int zlength, elength;

	  tp->stage++;
	  zlength = throttle_get_zline_time (tp->stage);

	  sendto_snomask (SNOMASK_REJECTS,
			      "throttled connections from %s (%d in %d seconds) for %d minutes (offense %d)",
			      tp->addr, tp->conns, sotime - tp->first,
			      zlength / 60, tp->stage + 1);

	  elength =
	    ircsnprintf (errbufr, 512,
			 ":%s NOTICE ZUSR :You have been throttled for %d minutes for too "
			 "many connections in a short period of time. Further connections in this period will reset "
			 "your throttle and you will have to wait longer.\r\n",
			 me.name, zlength / 60);
	  send (fd, errbufr, elength, 0);

	  if (throttle_get_zline_time (tp->stage + 1) != zlength)
	    {
	      elength =
		ircsnprintf (errbufr, 512,
			     ":%s NOTICE ZUSR :When you return, if you are throttled again, "
			     "your throttle will last longer.\r\n", me.name);
	      send (fd, errbufr, elength, 0);
	    }

	  /* We steal this message from undernet, because mIRC detects it and doesn't try to
	     autoreconnect */
	  elength =
	    ircsnprintf (errbufr, 512,
			 "ERROR :Your host is trying to (re)connect too fast -- throttled.\r\n",
			 tp->addr);
	  send (fd, errbufr, elength, 0);

	  tp->zline_start = sotime;
	}
      else
	{
	  /* it might be desireable at some point to let people know about
	   * these problems.  for now, however, don't. */
	}

      return 0;			/* drop 'em */
    }

  return 1;			/* they're okay. */
}

/* walk through our list of throttles, expire any as necessary.  in the case of
 * Z:lines, expire them at the end of the Z:line timeout period. */
/* Expire at the end of the zline timeout period plus throttle_rtime */
void
throttle_timer (time_t now)
{
  throttle *tp, *tp2;
  time_t zlength;

  if (!throttle_enable)
    return;

  tp = LIST_FIRST (&throttles);
  while (tp != NULL)
    {
      zlength = throttle_get_zline_time (tp->stage);
      tp2 = LIST_NEXT (tp, lp);
      if ((now == 0) ||
	  (tp->zline_start
	   && (now - tp->zline_start) >= (zlength + throttle_rtime))
	  || (!tp->zline_start && (now - tp->last) >= throttle_rtime))
	{
	  /* delete this item */
	  LIST_REMOVE (tp, lp);
	  hash_delete (throttle_hash, tp);
	  throttle_free (tp);
	  numthrottles--;
	}
      tp = tp2;
    }
}

void
throttle_rehash (void)
{
  throttle_timer (0);
}

void
throttle_resize (int size)
{
  resize_hash_table (throttle_hash, size);
}

void
throttle_stats (aClient * client_p, char *name)
{
  int pending = 0, bans = 0;
  throttle *tp;
  unsigned int tcnt, tsz, hcnt, hsz;

  tcnt = throttle_freelist->blocksAllocated *
    throttle_freelist->elemsPerBlock;
  tsz = tcnt * throttle_freelist->elemSize;

  hcnt = hashent_freelist->blocksAllocated * hashent_freelist->elemsPerBlock;
  hsz = hcnt * hashent_freelist->elemSize;

  sendto_one (client_p, ":%s %d %s :throttles: %d", me.name, RPL_STATSDEBUG,
	      name, numthrottles);
  sendto_one (client_p,
	      ":%s %d %s :alloc memory: %d throttles (%d bytes), "
	      "%d hashents (%d bytes)", me.name, RPL_STATSDEBUG, name, tcnt,
	      tsz, hcnt, hsz);
  sendto_one (client_p, ":%s %d %s :throttle hash table size: %d", me.name,
	      RPL_STATSDEBUG, name, throttle_hash->size);

  /* now count bans/pending */
  LIST_FOREACH (tp, &throttles, lp)
  {
    if (tp->zline_start)
      bans++;
    else
      pending++;
  }
  sendto_one (client_p, ":%s %d %s :throttles pending=%d bans=%d", me.name,
	      RPL_STATSDEBUG, name, pending, bans);
  LIST_FOREACH (tp, &throttles, lp)
  {
    int ztime = throttle_get_zline_time (tp->stage);

    if (tp->zline_start && tp->zline_start + ztime > NOW)
      sendto_one (client_p,
		  ":%s %d %s :throttled: %s [stage %d, %d secs remain, %d futile retries]",
		  me.name, RPL_STATSDEBUG, name, tp->addr, tp->stage,
		  (tp->zline_start + ztime) - NOW, tp->re_zlines);
  }
}

#endif
/* vi:set ts=8 sts=4 sw=4 tw=79: */
