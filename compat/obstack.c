/* obstack.c - subroutines used implicitly by object stack macros
   Copyright (C) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include "git-compat-util.h"
#include <gettext.h>
#include "obstack.h"

/* NOTE BEFORE MODIFYING THIS FILE: This version number must be
   incremented whenever callers compiled using an old obstack.h can no
   longer properly call the functions in this obstack.c.  */
#define OBSTACK_INTERFACE_VERSION 1

/* Comment out all this code if we are using the GNU C Library, and are not
   actually compiling the library itself, and the installed library
   supports the same library interface we do.  This code is part of the GNU
   C Library, but also included in many other GNU distributions.  Compiling
   and linking in this code is a waste when using the GNU C library
   (especially if it is a shared library).  Rather than having every GNU
   program understand `configure --with-gnu-libc' and omit the object
   files, it is simpler to just do this in the source for each such file.  */

#include <stdio.h>		/* Random thing to get __GNU_LIBRARY__.  */
#if !defined _LIBC && defined __GNU_LIBRARY__ && __GNU_LIBRARY__ > 1
# include <gnu-versions.h>
# if _GNU_OBSTACK_INTERFACE_VERSION == OBSTACK_INTERFACE_VERSION
#  define ELIDE_CODE
# endif
#endif

#include <stddef.h>

#ifndef ELIDE_CODE


# if HAVE_INTTYPES_H
#  include <inttypes.h>
# endif
# if HAVE_STDINT_H || defined _LIBC
#  include <stdint.h>
# endif

/* Determine default alignment.  */
union fooround
{
  uintmax_t i;
  long double d;
  void *p;
};
struct fooalign
{
  char c;
  union fooround u;
};
/* If malloc were really smart, it would round addresses to DEFAULT_ALIGNMENT.
   But in fact it might be less smart and round addresses to as much as
   DEFAULT_ROUNDING.  So we prepare for it to do that.  */
enum
  {
    DEFAULT_ALIGNMENT = offsetof (struct fooalign, u),
    DEFAULT_ROUNDING = sizeof (union fooround)
  };

/* When we copy a long block of data, this is the unit to do it with.
   On some machines, copying successive ints does not work;
   in such a case, redefine COPYING_UNIT to `long' (if that works)
   or `char' as a last resort.  */
# ifndef COPYING_UNIT
#  define COPYING_UNIT int
# endif


/* The functions allocating more room by calling `obstack_chunk_alloc'
   jump to the handler pointed to by `obstack_alloc_failed_handler'.
   This can be set to a user defined function which should either
   abort gracefully or use longjump - but shouldn't return.  This
   variable by default points to the internal function
   `print_and_abort'.  */
static void print_and_abort (void);
void (*obstack_alloc_failed_handler) (void) = print_and_abort;

# ifdef _LIBC
#  if SHLIB_COMPAT (libc, GLIBC_2_0, GLIBC_2_3_4)
/* A looong time ago (before 1994, anyway; we're not sure) this global variable
   was used by non-GNU-C macros to avoid multiple evaluation.  The GNU C
   library still exports it because somebody might use it.  */
struct obstack *_obstack_compat;
compat_symbol (libc, _obstack_compat, _obstack, GLIBC_2_0);
#  endif
# endif

/* Define a macro that either calls functions with the traditional malloc/free
   calling interface, or calls functions with the mmalloc/mfree interface
   (that adds an extra first argument), based on the state of use_extra_arg.
   For free, do not use ?:, since some compilers, like the MIPS compilers,
   do not allow (expr) ? void : void.  */

# define CALL_CHUNKFUN(h, size) \
  (((h) -> use_extra_arg) \
   ? (*(h)->chunkfun.extra) ((h)->extra_arg, (size)) \
   : (*(h)->chunkfun.plain) ((size)))

# define CALL_FREEFUN(h, old_chunk) \
  do { \
    if ((h) -> use_extra_arg) \
      (*(h)->freefun.extra) ((h)->extra_arg, (old_chunk)); \
    else \
      (*(h)->freefun.plain) ((old_chunk)); \
  } while (0)


/* Initialize an obstack H for use.  Specify chunk size SIZE (0 means default).
   Objects start on multiples of ALIGNMENT (0 means use default).
   CHUNKFUN is the function to use to allocate chunks,
   and FREEFUN the function to free them.

   Return nonzero if successful, calls obstack_alloc_failed_handler if
   allocation fails.  */

int
_obstack_begin (struct obstack *h,
		int size, int alignment,
		void *(*chunkfun) (long),
		void (*freefun) (void *))
{
  register struct _obstack_chunk *chunk; /* points to new chunk */

  if (alignment == 0)
    alignment = DEFAULT_ALIGNMENT;
  if (size == 0)
    /* Default size is what GNU malloc can fit in a 4096-byte block.  */
    {
      /* 12 is sizeof (mhead) and 4 is EXTRA from GNU malloc.
	 Use the values for range checking, because if range checking is off,
	 the extra bytes won't be missed terribly, but if range checking is on
	 and we used a larger request, a whole extra 4096 bytes would be
	 allocated.

	 These number are irrelevant to the new GNU malloc.  I suspect it is
	 less sensitive to the size of the request.  */
      int extra = ((((12 + DEFAULT_ROUNDING - 1) & ~(DEFAULT_ROUNDING - 1))
		    + 4 + DEFAULT_ROUNDING - 1)
		   & ~(DEFAULT_ROUNDING - 1));
      size = 4096 - extra;
    }

  h->chunkfun.plain = chunkfun;
  h->freefun.plain = freefun;
  h->chunk_size = size;
  h->alignment_mask = alignment - 1;
  h->use_extra_arg = 0;

  chunk = h->chunk = CALL_CHUNKFUN (h, h -> chunk_size);
  if (!chunk)
    (*obstack_alloc_failed_handler) ();
  h->next_free = h->object_base = __PTR_ALIGN ((char *) chunk, chunk->contents,
					       alignment - 1);
  h->chunk_limit = chunk->limit
    = (char *) chunk + h->chunk_size;
  chunk->prev = NULL;
  /* The initial chunk now contains no empty object.  */
  h->maybe_empty_object = 0;
  h->alloc_failed = 0;
  return 1;
}

int
_obstack_begin_1 (struct obstack *h, int size, int alignment,
		  void *(*chunkfun) (void *, long),
		  void (*freefun) (void *, void *),
		  void *arg)
{
  register struct _obstack_chunk *chunk; /* points to new chunk */

  if (alignment == 0)
    alignment = DEFAULT_ALIGNMENT;
  if (size == 0)
    /* Default size is what GNU malloc can fit in a 4096-byte block.  */
    {
      /* 12 is sizeof (mhead) and 4 is EXTRA from GNU malloc.
	 Use the values for range checking, because if range checking is off,
	 the extra bytes won't be missed terribly, but if range checking is on
	 and we used a larger request, a whole extra 4096 bytes would be
	 allocated.

	 These number are irrelevant to the new GNU malloc.  I suspect it is
	 less sensitive to the size of the request.  */
      int extra = ((((12 + DEFAULT_ROUNDING - 1) & ~(DEFAULT_ROUNDING - 1))
		    + 4 + DEFAULT_ROUNDING - 1)
		   & ~(DEFAULT_ROUNDING - 1));
      size = 4096 - extra;
    }

  h->chunkfun.extra = (struct _obstack_chunk * (*)(void *,long)) chunkfun;
  h->freefun.extra = (void (*) (void *, struct _obstack_chunk *)) freefun;

  h->chunk_size = size;
  h->alignment_mask = alignment - 1;
  h->extra_arg = arg;
  h->use_extra_arg = 1;

  chunk = h->chunk = CALL_CHUNKFUN (h, h -> chunk_size);
  if (!chunk)
    (*obstack_alloc_failed_handler) ();
  h->next_free = h->object_base = __PTR_ALIGN ((char *) chunk, chunk->contents,
					       alignment - 1);
  h->chunk_limit = chunk->limit
    = (char *) chunk + h->chunk_size;
  chunk->prev = NULL;
  /* The initial chunk now contains no empty object.  */
  h->maybe_empty_object = 0;
  h->alloc_failed = 0;
  return 1;
}

/* Allocate a new current chunk for the obstack *H
   on the assumption that LENGTH bytes need to be added
   to the current object, or a new object of length LENGTH allocated.
   Copies any partial object from the end of the old chunk
   to the beginning of the new one.  */

void
_obstack_newchunk (struct obstack *h, int length)
{
  register struct _obstack_chunk *old_chunk = h->chunk;
  register struct _obstack_chunk *new_chunk;
  register long	new_size;
  register long obj_size = h->next_free - h->object_base;
  register long i;
  long already;
  char *object_base;

  /* Compute size for new chunk.  */
  new_size = (obj_size + length) + (obj_size >> 3) + h->alignment_mask + 100;
  if (new_size < h->chunk_size)
    new_size = h->chunk_size;

  /* Allocate and initialize the new chunk.  */
  new_chunk = CALL_CHUNKFUN (h, new_size);
  if (!new_chunk)
    (*obstack_alloc_failed_handler) ();
  h->chunk = new_chunk;
  new_chunk->prev = old_chunk;
  new_chunk->limit = h->chunk_limit = (char *) new_chunk + new_size;

  /* Compute an aligned object_base in the new chunk */
  object_base =
    __PTR_ALIGN ((char *) new_chunk, new_chunk->contents, h->alignment_mask);

  /* Move the existing object to the new chunk.
     Word at a time is fast and is safe if the object
     is sufficiently aligned.  */
  if (h->alignment_mask + 1 >= DEFAULT_ALIGNMENT)
    {
      for (i = obj_size / sizeof (COPYING_UNIT) - 1;
	   i >= 0; i--)
	((COPYING_UNIT *)object_base)[i]
	  = ((COPYING_UNIT *)h->object_base)[i];
      /* We used to copy the odd few remaining bytes as one extra COPYING_UNIT,
	 but that can cross a page boundary on a machine
	 which does not do strict alignment for COPYING_UNITS.  */
      already = obj_size / sizeof (COPYING_UNIT) * sizeof (COPYING_UNIT);
    }
  else
    already = 0;
  /* Copy remaining bytes one by one.  */
  for (i = already; i < obj_size; i++)
    object_base[i] = h->object_base[i];

  /* If the object just copied was the only data in OLD_CHUNK,
     free that chunk and remove it from the chain.
     But not if that chunk might contain an empty object.  */
  if (! h->maybe_empty_object
      && (h->object_base
	  == __PTR_ALIGN ((char *) old_chunk, old_chunk->contents,
			  h->alignment_mask)))
    {
      new_chunk->prev = old_chunk->prev;
      CALL_FREEFUN (h, old_chunk);
    }

  h->object_base = object_base;
  h->next_free = h->object_base + obj_size;
  /* The new chunk certainly contains no empty object yet.  */
  h->maybe_empty_object = 0;
}
# ifdef _LIBC
libc_hidden_def (_obstack_newchunk)
# endif

/* Return nonzero if object OBJ has been allocated from obstack H.
   This is here for debugging.
   If you use it in a program, you are probably losing.  */

/* Suppress -Wmissing-prototypes warning.  We don't want to declare this in
   obstack.h because it is just for debugging.  */
int _obstack_allocated_p (struct obstack *h, void *obj);

int
_obstack_allocated_p (struct obstack *h, void *obj)
{
  register struct _obstack_chunk *lp;	/* below addr of any objects in this chunk */
  register struct _obstack_chunk *plp;	/* point to previous chunk if any */

  lp = (h)->chunk;
  /* We use >= rather than > since the object cannot be exactly at
     the beginning of the chunk but might be an empty object exactly
     at the end of an adjacent chunk.  */
  while (lp != NULL && ((void *) lp >= obj || (void *) (lp)->limit < obj))
    {
      plp = lp->prev;
      lp = plp;
    }
  return lp != NULL;
}

/* Free objects in obstack H, including OBJ and everything allocate
   more recently than OBJ.  If OBJ is zero, free everything in H.  */

# undef obstack_free

void
obstack_free (struct obstack *h, void *obj)
{
  register struct _obstack_chunk *lp;	/* below addr of any objects in this chunk */
  register struct _obstack_chunk *plp;	/* point to previous chunk if any */

  lp = h->chunk;
  /* We use >= because there cannot be an object at the beginning of a chunk.
     But there can be an empty object at that address
     at the end of another chunk.  */
  while (lp != NULL && ((void *) lp >= obj || (void *) (lp)->limit < obj))
    {
      plp = lp->prev;
      CALL_FREEFUN (h, lp);
      lp = plp;
      /* If we switch chunks, we can't tell whether the new current
	 chunk contains an empty object, so assume that it may.  */
      h->maybe_empty_object = 1;
    }
  if (lp)
    {
      h->object_base = h->next_free = (char *) (obj);
      h->chunk_limit = lp->limit;
      h->chunk = lp;
    }
  else if (obj != NULL)
    /* obj is not in any of the chunks! */
    abort ();
}

# ifdef _LIBC
/* Older versions of libc used a function _obstack_free intended to be
   called by non-GCC compilers.  */
strong_alias (obstack_free, _obstack_free)
# endif

int
_obstack_memory_used (struct obstack *h)
{
  register struct _obstack_chunk* lp;
  register int nbytes = 0;

  for (lp = h->chunk; lp != NULL; lp = lp->prev)
    {
      nbytes += lp->limit - (char *) lp;
    }
  return nbytes;
}

# ifdef _LIBC
#  include <libio/iolibio.h>
# endif

# ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
#  if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5)
#   define __attribute__(Spec) /* empty */
#  endif
# endif

static void
print_and_abort (void)
{
  /* Don't change any of these strings.  Yes, it would be possible to add
     the newline to the string and use fputs or so.  But this must not
     happen because the "memory exhausted" message appears in other places
     like this and the translation should be reused instead of creating
     a very similar string which requires a separate translation.  */
# ifdef _LIBC
  (void) __fxprintf (NULL, "%s\n", _("memory exhausted"));
# else
  fprintf (stderr, "%s\n", _("memory exhausted"));
# endif
  exit (1);
}

#endif	/* !ELIDE_CODE */
