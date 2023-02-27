/*
 * This file has been copied from commit e7ac713d^ in the GNU grep git
 * repository. A few small changes have been made to adapt the code to
 * Git.
 */

/* kwset.c - search for any of a set of keywords.
   Copyright 1989, 1998, 2000, 2005 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>. */

/* Written August 1989 by Mike Haertel.
   The author may be reached (Email) at the address mike@ai.mit.edu,
   or (US mail) as Mike Haertel c/o Free Software Foundation. */

/* The algorithm implemented by these routines bears a startling resemblance
   to one discovered by Beate Commentz-Walter, although it is not identical.
   See "A String Matching Algorithm Fast on the Average," Technical Report,
   IBM-Germany, Scientific Center Heidelberg, Tiergartenstrasse 15, D-6900
   Heidelberg, Germany.  See also Aho, A.V., and M. Corasick, "Efficient
   String Matching:  An Aid to Bibliographic Search," CACM June 1975,
   Vol. 18, No. 6, which describes the failure function used below. */

#include "git-compat-util.h"

#include "kwset.h"
#include "compat/obstack.h"

#define NCHAR (UCHAR_MAX + 1)
/* adapter for `xmalloc()`, which takes `size_t`, not `long` */
static void *obstack_chunk_alloc(long size)
{
	if (size < 0)
		BUG("Cannot allocate a negative amount: %ld", size);
	return xmalloc(size);
}
#define obstack_chunk_free free

#define U(c) ((unsigned char) (c))

/* Balanced tree of edges and labels leaving a given trie node. */
struct tree
{
  struct tree *llink;		/* Left link; MUST be first field. */
  struct tree *rlink;		/* Right link (to larger labels). */
  struct trie *trie;		/* Trie node pointed to by this edge. */
  unsigned char label;		/* Label on this edge. */
  char balance;			/* Difference in depths of subtrees. */
};

/* Node of a trie representing a set of reversed keywords. */
struct trie
{
  unsigned int accepting;	/* Word index of accepted word, or zero. */
  struct tree *links;		/* Tree of edges leaving this node. */
  struct trie *parent;		/* Parent of this node. */
  struct trie *next;		/* List of all trie nodes in level order. */
  struct trie *fail;		/* Aho-Corasick failure function. */
  int depth;			/* Depth of this node from the root. */
  int shift;			/* Shift function for search failures. */
  int maxshift;			/* Max shift of self and descendants. */
};

/* Structure returned opaquely to the caller, containing everything. */
struct kwset
{
  struct obstack obstack;	/* Obstack for node allocation. */
  int words;			/* Number of words in the trie. */
  struct trie *trie;		/* The trie itself. */
  int mind;			/* Minimum depth of an accepting node. */
  int maxd;			/* Maximum depth of any node. */
  unsigned char delta[NCHAR];	/* Delta table for rapid search. */
  struct trie *next[NCHAR];	/* Table of children of the root. */
  char *target;			/* Target string if there's only one. */
  int mind2;			/* Used in Boyer-Moore search for one string. */
  unsigned char const *trans;  /* Character translation table. */
};

/* Allocate and initialize a keyword set object, returning an opaque
   pointer to it.  Return NULL if memory is not available. */
kwset_t
kwsalloc (unsigned char const *trans)
{
  struct kwset *kwset;

  kwset = (struct kwset *) xmalloc(sizeof (struct kwset));

  obstack_init(&kwset->obstack);
  kwset->words = 0;
  kwset->trie
    = (struct trie *) obstack_alloc(&kwset->obstack, sizeof (struct trie));
  if (!kwset->trie)
    {
      kwsfree((kwset_t) kwset);
      return NULL;
    }
  kwset->trie->accepting = 0;
  kwset->trie->links = NULL;
  kwset->trie->parent = NULL;
  kwset->trie->next = NULL;
  kwset->trie->fail = NULL;
  kwset->trie->depth = 0;
  kwset->trie->shift = 0;
  kwset->mind = INT_MAX;
  kwset->maxd = -1;
  kwset->target = NULL;
  kwset->trans = trans;

  return (kwset_t) kwset;
}

/* This upper bound is valid for CHAR_BIT >= 4 and
   exact for CHAR_BIT in { 4..11, 13, 15, 17, 19 }. */
#define DEPTH_SIZE (CHAR_BIT + CHAR_BIT/2)

/* Add the given string to the contents of the keyword set.  Return NULL
   for success, an error message otherwise. */
const char *
kwsincr (kwset_t kws, char const *text, size_t len)
{
  struct kwset *kwset;
  register struct trie *trie;
  register unsigned char label;
  register struct tree *link;
  register int depth;
  struct tree *links[DEPTH_SIZE];
  enum { L, R } dirs[DEPTH_SIZE];
  struct tree *t, *r, *l, *rl, *lr;

  kwset = (struct kwset *) kws;
  trie = kwset->trie;
  text += len;

  /* Descend the trie (built of reversed keywords) character-by-character,
     installing new nodes when necessary. */
  while (len--)
    {
      label = kwset->trans ? kwset->trans[U(*--text)] : *--text;

      /* Descend the tree of outgoing links for this trie node,
	 looking for the current character and keeping track
	 of the path followed. */
      link = trie->links;
      links[0] = (struct tree *) &trie->links;
      dirs[0] = L;
      depth = 1;

      while (link && label != link->label)
	{
	  links[depth] = link;
	  if (label < link->label)
	    dirs[depth++] = L, link = link->llink;
	  else
	    dirs[depth++] = R, link = link->rlink;
	}

      /* The current character doesn't have an outgoing link at
	 this trie node, so build a new trie node and install
	 a link in the current trie node's tree. */
      if (!link)
	{
	  link = (struct tree *) obstack_alloc(&kwset->obstack,
					       sizeof (struct tree));
	  if (!link)
	    return "memory exhausted";
	  link->llink = NULL;
	  link->rlink = NULL;
	  link->trie = (struct trie *) obstack_alloc(&kwset->obstack,
						     sizeof (struct trie));
	  if (!link->trie)
	    {
	      obstack_free(&kwset->obstack, link);
	      return "memory exhausted";
	    }
	  link->trie->accepting = 0;
	  link->trie->links = NULL;
	  link->trie->parent = trie;
	  link->trie->next = NULL;
	  link->trie->fail = NULL;
	  link->trie->depth = trie->depth + 1;
	  link->trie->shift = 0;
	  link->label = label;
	  link->balance = 0;

	  /* Install the new tree node in its parent. */
	  if (dirs[--depth] == L)
	    links[depth]->llink = link;
	  else
	    links[depth]->rlink = link;

	  /* Back up the tree fixing the balance flags. */
	  while (depth && !links[depth]->balance)
	    {
	      if (dirs[depth] == L)
		--links[depth]->balance;
	      else
		++links[depth]->balance;
	      --depth;
	    }

	  /* Rebalance the tree by pointer rotations if necessary. */
	  if (depth && ((dirs[depth] == L && --links[depth]->balance)
			|| (dirs[depth] == R && ++links[depth]->balance)))
	    {
	      switch (links[depth]->balance)
		{
		case (char) -2:
		  switch (dirs[depth + 1])
		    {
		    case L:
		      r = links[depth], t = r->llink, rl = t->rlink;
		      t->rlink = r, r->llink = rl;
		      t->balance = r->balance = 0;
		      break;
		    case R:
		      r = links[depth], l = r->llink, t = l->rlink;
		      rl = t->rlink, lr = t->llink;
		      t->llink = l, l->rlink = lr, t->rlink = r, r->llink = rl;
		      l->balance = t->balance != 1 ? 0 : -1;
		      r->balance = t->balance != (char) -1 ? 0 : 1;
		      t->balance = 0;
		      break;
		    default:
		      abort ();
		    }
		  break;
		case 2:
		  switch (dirs[depth + 1])
		    {
		    case R:
		      l = links[depth], t = l->rlink, lr = t->llink;
		      t->llink = l, l->rlink = lr;
		      t->balance = l->balance = 0;
		      break;
		    case L:
		      l = links[depth], r = l->rlink, t = r->llink;
		      lr = t->llink, rl = t->rlink;
		      t->llink = l, l->rlink = lr, t->rlink = r, r->llink = rl;
		      l->balance = t->balance != 1 ? 0 : -1;
		      r->balance = t->balance != (char) -1 ? 0 : 1;
		      t->balance = 0;
		      break;
		    default:
		      abort ();
		    }
		  break;
		default:
		  abort ();
		}

	      if (dirs[depth - 1] == L)
		links[depth - 1]->llink = t;
	      else
		links[depth - 1]->rlink = t;
	    }
	}

      trie = link->trie;
    }

  /* Mark the node we finally reached as accepting, encoding the
     index number of this word in the keyword set so far. */
  if (!trie->accepting)
    trie->accepting = 1 + 2 * kwset->words;
  ++kwset->words;

  /* Keep track of the longest and shortest string of the keyword set. */
  if (trie->depth < kwset->mind)
    kwset->mind = trie->depth;
  if (trie->depth > kwset->maxd)
    kwset->maxd = trie->depth;

  return NULL;
}

/* Enqueue the trie nodes referenced from the given tree in the
   given queue. */
static void
enqueue (struct tree *tree, struct trie **last)
{
  if (!tree)
    return;
  enqueue(tree->llink, last);
  enqueue(tree->rlink, last);
  (*last) = (*last)->next = tree->trie;
}

/* Compute the Aho-Corasick failure function for the trie nodes referenced
   from the given tree, given the failure function for their parent as
   well as a last resort failure node. */
static void
treefails (register struct tree const *tree, struct trie const *fail,
	   struct trie *recourse)
{
  register struct tree *link;

  if (!tree)
    return;

  treefails(tree->llink, fail, recourse);
  treefails(tree->rlink, fail, recourse);

  /* Find, in the chain of fails going back to the root, the first
     node that has a descendant on the current label. */
  while (fail)
    {
      link = fail->links;
      while (link && tree->label != link->label)
	if (tree->label < link->label)
	  link = link->llink;
	else
	  link = link->rlink;
      if (link)
	{
	  tree->trie->fail = link->trie;
	  return;
	}
      fail = fail->fail;
    }

  tree->trie->fail = recourse;
}

/* Set delta entries for the links of the given tree such that
   the preexisting delta value is larger than the current depth. */
static void
treedelta (register struct tree const *tree,
	   register unsigned int depth,
	   unsigned char delta[])
{
  if (!tree)
    return;
  treedelta(tree->llink, depth, delta);
  treedelta(tree->rlink, depth, delta);
  if (depth < delta[tree->label])
    delta[tree->label] = depth;
}

/* Return true if A has every label in B. */
static int
hasevery (register struct tree const *a, register struct tree const *b)
{
  if (!b)
    return 1;
  if (!hasevery(a, b->llink))
    return 0;
  if (!hasevery(a, b->rlink))
    return 0;
  while (a && b->label != a->label)
    if (b->label < a->label)
      a = a->llink;
    else
      a = a->rlink;
  return !!a;
}

/* Compute a vector, indexed by character code, of the trie nodes
   referenced from the given tree. */
static void
treenext (struct tree const *tree, struct trie *next[])
{
  if (!tree)
    return;
  treenext(tree->llink, next);
  treenext(tree->rlink, next);
  next[tree->label] = tree->trie;
}

/* Compute the shift for each trie node, as well as the delta
   table and next cache for the given keyword set. */
const char *
kwsprep (kwset_t kws)
{
  register struct kwset *kwset;
  register int i;
  register struct trie *curr;
  register unsigned char const *trans;
  unsigned char delta[NCHAR];

  kwset = (struct kwset *) kws;

  /* Initial values for the delta table; will be changed later.  The
     delta entry for a given character is the smallest depth of any
     node at which an outgoing edge is labeled by that character. */
  memset(delta, kwset->mind < UCHAR_MAX ? kwset->mind : UCHAR_MAX, NCHAR);

  /* Check if we can use the simple boyer-moore algorithm, instead
     of the hairy commentz-walter algorithm. */
  if (kwset->words == 1 && kwset->trans == NULL)
    {
      char c;

      /* Looking for just one string.  Extract it from the trie. */
      kwset->target = obstack_alloc(&kwset->obstack, kwset->mind);
      if (!kwset->target)
	return "memory exhausted";
      for (i = kwset->mind - 1, curr = kwset->trie; i >= 0; --i)
	{
	  kwset->target[i] = curr->links->label;
	  curr = curr->links->trie;
	}
      /* Build the Boyer Moore delta.  Boy that's easy compared to CW. */
      for (i = 0; i < kwset->mind; ++i)
	delta[U(kwset->target[i])] = kwset->mind - (i + 1);
      /* Find the minimal delta2 shift that we might make after
	 a backwards match has failed. */
      c = kwset->target[kwset->mind - 1];
      for (i = kwset->mind - 2; i >= 0; --i)
	if (kwset->target[i] == c)
	  break;
      kwset->mind2 = kwset->mind - (i + 1);
    }
  else
    {
      register struct trie *fail;
      struct trie *last, *next[NCHAR];

      /* Traverse the nodes of the trie in level order, simultaneously
	 computing the delta table, failure function, and shift function. */
      for (curr = last = kwset->trie; curr; curr = curr->next)
	{
	  /* Enqueue the immediate descendants in the level order queue. */
	  enqueue(curr->links, &last);

	  curr->shift = kwset->mind;
	  curr->maxshift = kwset->mind;

	  /* Update the delta table for the descendants of this node. */
	  treedelta(curr->links, curr->depth, delta);

	  /* Compute the failure function for the descendants of this node. */
	  treefails(curr->links, curr->fail, kwset->trie);

	  /* Update the shifts at each node in the current node's chain
	     of fails back to the root. */
	  for (fail = curr->fail; fail; fail = fail->fail)
	    {
	      /* If the current node has some outgoing edge that the fail
		 doesn't, then the shift at the fail should be no larger
		 than the difference of their depths. */
	      if (!hasevery(fail->links, curr->links))
		if (curr->depth - fail->depth < fail->shift)
		  fail->shift = curr->depth - fail->depth;

	      /* If the current node is accepting then the shift at the
		 fail and its descendants should be no larger than the
		 difference of their depths. */
	      if (curr->accepting && fail->maxshift > curr->depth - fail->depth)
		fail->maxshift = curr->depth - fail->depth;
	    }
	}

      /* Traverse the trie in level order again, fixing up all nodes whose
	 shift exceeds their inherited maxshift. */
      for (curr = kwset->trie->next; curr; curr = curr->next)
	{
	  if (curr->maxshift > curr->parent->maxshift)
	    curr->maxshift = curr->parent->maxshift;
	  if (curr->shift > curr->maxshift)
	    curr->shift = curr->maxshift;
	}

      /* Create a vector, indexed by character code, of the outgoing links
	 from the root node. */
      for (i = 0; i < NCHAR; ++i)
	next[i] = NULL;
      treenext(kwset->trie->links, next);

      if ((trans = kwset->trans))
	for (i = 0; i < NCHAR; ++i)
	  kwset->next[i] = next[U(trans[i])];
      else
	COPY_ARRAY(kwset->next, next, NCHAR);
    }

  /* Fix things up for any translation table. */
  if ((trans = kwset->trans))
    for (i = 0; i < NCHAR; ++i)
      kwset->delta[i] = delta[U(trans[i])];
  else
    memcpy(kwset->delta, delta, NCHAR);

  return NULL;
}

/* Fast boyer-moore search. */
static size_t
bmexec (kwset_t kws, char const *text, size_t size)
{
  struct kwset const *kwset;
  register unsigned char const *d1;
  register char const *ep, *sp, *tp;
  register int d, gc, i, len, md2;

  kwset = (struct kwset const *) kws;
  len = kwset->mind;

  if (len == 0)
    return 0;
  if (len > size)
    return -1;
  if (len == 1)
    {
      tp = memchr (text, kwset->target[0], size);
      return tp ? tp - text : -1;
    }

  d1 = kwset->delta;
  sp = kwset->target + len;
  gc = U(sp[-2]);
  md2 = kwset->mind2;
  tp = text + len;

  /* Significance of 12: 1 (initial offset) + 10 (skip loop) + 1 (md2). */
  if (size > 12 * len)
    /* 11 is not a bug, the initial offset happens only once. */
    for (ep = text + size - 11 * len;;)
      {
	while (tp <= ep)
	  {
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    if (d == 0)
	      goto found;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    if (d == 0)
	      goto found;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    if (d == 0)
	      goto found;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	  }
	break;
      found:
	if (U(tp[-2]) == gc)
	  {
	    for (i = 3; i <= len && U(tp[-i]) == U(sp[-i]); ++i)
	      ;
	    if (i > len)
	      return tp - len - text;
	  }
	tp += md2;
      }

  /* Now we have only a few characters left to search.  We
     carefully avoid ever producing an out-of-bounds pointer. */
  ep = text + size;
  d = d1[U(tp[-1])];
  while (d <= ep - tp)
    {
      d = d1[U((tp += d)[-1])];
      if (d != 0)
	continue;
      if (U(tp[-2]) == gc)
	{
	  for (i = 3; i <= len && U(tp[-i]) == U(sp[-i]); ++i)
	    ;
	  if (i > len)
	    return tp - len - text;
	}
      d = md2;
    }

  return -1;
}

/* Hairy multiple string search. */
static size_t
cwexec (kwset_t kws, char const *text, size_t len, struct kwsmatch *kwsmatch)
{
  struct kwset const *kwset;
  struct trie * const *next;
  struct trie const *trie;
  struct trie const *accept;
  char const *beg, *lim, *mch, *lmch;
  register unsigned char c;
  register unsigned char const *delta;
  register int d;
  register char const *end, *qlim;
  register struct tree const *tree;
  register unsigned char const *trans;

  accept = NULL;

  /* Initialize register copies and look for easy ways out. */
  kwset = (struct kwset *) kws;
  if (len < kwset->mind)
    return -1;
  next = kwset->next;
  delta = kwset->delta;
  trans = kwset->trans;
  lim = text + len;
  end = text;
  if ((d = kwset->mind) != 0)
    mch = NULL;
  else
    {
      mch = text, accept = kwset->trie;
      goto match;
    }

  if (len >= 4 * kwset->mind)
    qlim = lim - 4 * kwset->mind;
  else
    qlim = NULL;

  while (lim - end >= d)
    {
      if (qlim && end <= qlim)
	{
	  end += d - 1;
	  while ((d = delta[c = *end]) && end < qlim)
	    {
	      end += d;
	      end += delta[U(*end)];
	      end += delta[U(*end)];
	    }
	  ++end;
	}
      else
	d = delta[c = (end += d)[-1]];
      if (d)
	continue;
      beg = end - 1;
      trie = next[c];
      if (trie->accepting)
	{
	  mch = beg;
	  accept = trie;
	}
      d = trie->shift;
      while (beg > text)
	{
	  c = trans ? trans[U(*--beg)] : *--beg;
	  tree = trie->links;
	  while (tree && c != tree->label)
	    if (c < tree->label)
	      tree = tree->llink;
	    else
	      tree = tree->rlink;
	  if (tree)
	    {
	      trie = tree->trie;
	      if (trie->accepting)
		{
		  mch = beg;
		  accept = trie;
		}
	    }
	  else
	    break;
	  d = trie->shift;
	}
      if (mch)
	goto match;
    }
  return -1;

 match:
  /* Given a known match, find the longest possible match anchored
     at or before its starting point.  This is nearly a verbatim
     copy of the preceding main search loops. */
  if (lim - mch > kwset->maxd)
    lim = mch + kwset->maxd;
  lmch = NULL;
  d = 1;
  while (lim - end >= d)
    {
      if ((d = delta[c = (end += d)[-1]]) != 0)
	continue;
      beg = end - 1;
      if (!(trie = next[c]))
	{
	  d = 1;
	  continue;
	}
      if (trie->accepting && beg <= mch)
	{
	  lmch = beg;
	  accept = trie;
	}
      d = trie->shift;
      while (beg > text)
	{
	  c = trans ? trans[U(*--beg)] : *--beg;
	  tree = trie->links;
	  while (tree && c != tree->label)
	    if (c < tree->label)
	      tree = tree->llink;
	    else
	      tree = tree->rlink;
	  if (tree)
	    {
	      trie = tree->trie;
	      if (trie->accepting && beg <= mch)
		{
		  lmch = beg;
		  accept = trie;
		}
	    }
	  else
	    break;
	  d = trie->shift;
	}
      if (lmch)
	{
	  mch = lmch;
	  goto match;
	}
      if (!d)
	d = 1;
    }

  if (kwsmatch)
    {
      kwsmatch->index = accept->accepting / 2;
      kwsmatch->offset[0] = mch - text;
      kwsmatch->size[0] = accept->depth;
    }
  return mch - text;
}

/* Search through the given text for a match of any member of the
   given keyword set.  Return a pointer to the first character of
   the matching substring, or NULL if no match is found.  If FOUNDLEN
   is non-NULL store in the referenced location the length of the
   matching substring.  Similarly, if FOUNDIDX is non-NULL, store
   in the referenced location the index number of the particular
   keyword matched. */
size_t
kwsexec (kwset_t kws, char const *text, size_t size,
	 struct kwsmatch *kwsmatch)
{
  struct kwset const *kwset = (struct kwset *) kws;
  if (kwset->words == 1 && kwset->trans == NULL)
    {
      size_t ret = bmexec (kws, text, size);
      if (kwsmatch != NULL && ret != (size_t) -1)
	{
	  kwsmatch->index = 0;
	  kwsmatch->offset[0] = ret;
	  kwsmatch->size[0] = kwset->mind;
	}
      return ret;
    }
  else
    return cwexec(kws, text, size, kwsmatch);
}

/* Free the components of the given keyword set. */
void
kwsfree (kwset_t kws)
{
  struct kwset *kwset;

  kwset = (struct kwset *) kws;
  obstack_free(&kwset->obstack, NULL);
  free(kws);
}
