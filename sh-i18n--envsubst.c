/*
 * sh-i18n--envsubst.c - a stripped-down version of gettext's envsubst(1)
 *
 * Copyright (C) 2010 Ævar Arnfjörð Bjarmason
 *
 * This is a modified version of
 * 67d0871a8c:gettext-runtime/src/envsubst.c from the gettext.git
 * repository. It has been stripped down to only implement the
 * envsubst(1) features that we need in the git-sh-i18n fallbacks.
 *
 * The "Close standard error" part in main() is from
 * 8dac033df0:gnulib-local/lib/closeout.c. The copyright notices for
 * both files are reproduced immediately below.
 */

#include "git-compat-util.h"
#include "trace2.h"

/* Substitution of environment variables in shell format strings.
   Copyright (C) 2003-2007 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2003.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.  */

/* closeout.c - close standard output and standard error
   Copyright (C) 1998-2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* If true, substitution shall be performed on all variables.  */
static unsigned short int all_variables;

/* Forward declaration of local functions.  */
static void print_variables (const char *string);
static void note_variables (const char *string);
static void subst_from_stdin (void);

int
cmd_main (int argc, const char *argv[])
{
  /* Default values for command line options.  */
  /* unsigned short int show_variables = 0; */

  trace2_cmd_name("sh-i18n--envsubst");

  switch (argc)
	{
	case 1:
	  error ("we won't substitute all variables on stdin for you");
	  break;
	  /*
	  all_variables = 1;
      subst_from_stdin ();
	  */
	case 2:
	  /* echo '$foo and $bar' | git sh-i18n--envsubst --variables '$foo and $bar' */
	  all_variables = 0;
	  note_variables (argv[1]);
      subst_from_stdin ();
	  break;
	case 3:
	  /* git sh-i18n--envsubst --variables '$foo and $bar' */
	  if (strcmp(argv[1], "--variables"))
		error ("first argument must be --variables when two are given");
	  /* show_variables = 1; */
      print_variables (argv[2]);
	  break;
	default:
	  error ("too many arguments");
	  break;
	}

  /* Close standard error.  This is simpler than fwriteerror_no_ebadf, because
     upon failure we don't need an errno - all we can do at this point is to
     set an exit status.  */
  errno = 0;
  if (ferror (stderr) || fflush (stderr))
    {
      fclose (stderr);
      exit (EXIT_FAILURE);
    }
  if (fclose (stderr) && errno != EBADF)
    exit (EXIT_FAILURE);

  exit (EXIT_SUCCESS);
}

/* Parse the string and invoke the callback each time a $VARIABLE or
   ${VARIABLE} construct is seen, where VARIABLE is a nonempty sequence
   of ASCII alphanumeric/underscore characters, starting with an ASCII
   alphabetic/underscore character.
   We allow only ASCII characters, to avoid dependencies w.r.t. the current
   encoding: While "${\xe0}" looks like a variable access in ISO-8859-1
   encoding, it doesn't look like one in the BIG5, BIG5-HKSCS, GBK, GB18030,
   SHIFT_JIS, JOHAB encodings, because \xe0\x7d is a single character in these
   encodings.  */
static void
find_variables (const char *string,
		void (*callback) (const char *var_ptr, size_t var_len))
{
  for (; *string != '\0';)
    if (*string++ == '$')
      {
	const char *variable_start;
	const char *variable_end;
	unsigned short int valid;
	char c;

	if (*string == '{')
	  string++;

	variable_start = string;
	c = *string;
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
	  {
	    do
	      c = *++string;
	    while ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
		   || (c >= '0' && c <= '9') || c == '_');
	    variable_end = string;

	    if (variable_start[-1] == '{')
	      {
		if (*string == '}')
		  {
		    string++;
		    valid = 1;
		  }
		else
		  valid = 0;
	      }
	    else
	      valid = 1;

	    if (valid)
	      callback (variable_start, variable_end - variable_start);
	  }
      }
}


/* Print a variable to stdout, followed by a newline.  */
static void
print_variable (const char *var_ptr, size_t var_len)
{
  fwrite (var_ptr, var_len, 1, stdout);
  putchar ('\n');
}

/* Print the variables contained in STRING to stdout, each one followed by a
   newline.  */
static void
print_variables (const char *string)
{
  find_variables (string, &print_variable);
}


/* Type describing list of immutable strings,
   implemented using a dynamic array.  */
typedef struct string_list_ty string_list_ty;
struct string_list_ty
{
  const char **item;
  size_t nitems;
  size_t nitems_max;
};

/* Initialize an empty list of strings.  */
static inline void
string_list_init (string_list_ty *slp)
{
  slp->item = NULL;
  slp->nitems = 0;
  slp->nitems_max = 0;
}

/* Append a single string to the end of a list of strings.  */
static inline void
string_list_append (string_list_ty *slp, const char *s)
{
  /* Grow the list.  */
  if (slp->nitems >= slp->nitems_max)
    {
      slp->nitems_max = slp->nitems_max * 2 + 4;
      REALLOC_ARRAY(slp->item, slp->nitems_max);
    }

  /* Add the string to the end of the list.  */
  slp->item[slp->nitems++] = s;
}

/* Compare two strings given by reference.  */
static int
cmp_string (const void *pstr1, const void *pstr2)
{
  const char *str1 = *(const char **)pstr1;
  const char *str2 = *(const char **)pstr2;

  return strcmp (str1, str2);
}

/* Sort a list of strings.  */
static inline void
string_list_sort (string_list_ty *slp)
{
  QSORT(slp->item, slp->nitems, cmp_string);
}

/* Test whether a sorted string list contains a given string.  */
static int
sorted_string_list_member (const string_list_ty *slp, const char *s)
{
  size_t j1, j2;

  j1 = 0;
  j2 = slp->nitems;
  if (j2 > 0)
    {
      /* Binary search.  */
      while (j2 - j1 > 1)
	{
	  /* Here we know that if s is in the list, it is at an index j
	     with j1 <= j < j2.  */
	  size_t j = (j1 + j2) >> 1;
	  int result = strcmp (slp->item[j], s);

	  if (result > 0)
	    j2 = j;
	  else if (result == 0)
	    return 1;
	  else
	    j1 = j + 1;
	}
      if (j2 > j1)
	if (strcmp (slp->item[j1], s) == 0)
	  return 1;
    }
  return 0;
}


/* Set of variables on which to perform substitution.
   Used only if !all_variables.  */
static string_list_ty variables_set;

/* Adds a variable to variables_set.  */
static void
note_variable (const char *var_ptr, size_t var_len)
{
  char *string = xmemdupz (var_ptr, var_len);

  string_list_append (&variables_set, string);
}

/* Stores the variables occurring in the string in variables_set.  */
static void
note_variables (const char *string)
{
  string_list_init (&variables_set);
  find_variables (string, &note_variable);
  string_list_sort (&variables_set);
}


static int
do_getc (void)
{
  int c = getc (stdin);

  if (c == EOF)
    {
      if (ferror (stdin))
	error ("error while reading standard input");
    }

  return c;
}

static inline void
do_ungetc (int c)
{
  if (c != EOF)
    ungetc (c, stdin);
}

/* Copies stdin to stdout, performing substitutions.  */
static void
subst_from_stdin (void)
{
  static char *buffer;
  static size_t bufmax;
  static size_t buflen;
  int c;

  for (;;)
    {
      c = do_getc ();
      if (c == EOF)
	break;
      /* Look for $VARIABLE or ${VARIABLE}.  */
      if (c == '$')
	{
	  unsigned short int opening_brace = 0;
	  unsigned short int closing_brace = 0;

	  c = do_getc ();
	  if (c == '{')
	    {
	      opening_brace = 1;
	      c = do_getc ();
	    }
	  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
	    {
	      unsigned short int valid;

	      /* Accumulate the VARIABLE in buffer.  */
	      buflen = 0;
	      do
		{
		  if (buflen >= bufmax)
		    {
		      bufmax = 2 * bufmax + 10;
		      buffer = xrealloc (buffer, bufmax);
		    }
		  buffer[buflen++] = c;

		  c = do_getc ();
		}
	      while ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
		     || (c >= '0' && c <= '9') || c == '_');

	      if (opening_brace)
		{
		  if (c == '}')
		    {
		      closing_brace = 1;
		      valid = 1;
		    }
		  else
		    {
		      valid = 0;
		      do_ungetc (c);
		    }
		}
	      else
		{
		  valid = 1;
		  do_ungetc (c);
		}

	      if (valid)
		{
		  /* Terminate the variable in the buffer.  */
		  if (buflen >= bufmax)
		    {
		      bufmax = 2 * bufmax + 10;
		      buffer = xrealloc (buffer, bufmax);
		    }
		  buffer[buflen] = '\0';

		  /* Test whether the variable shall be substituted.  */
		  if (!all_variables
		      && !sorted_string_list_member (&variables_set, buffer))
		    valid = 0;
		}

	      if (valid)
		{
		  /* Substitute the variable's value from the environment.  */
		  const char *env_value = getenv (buffer);

		  if (env_value != NULL)
		    fputs (env_value, stdout);
		}
	      else
		{
		  /* Perform no substitution at all.  Since the buffered input
		     contains no other '$' than at the start, we can just
		     output all the buffered contents.  */
		  putchar ('$');
		  if (opening_brace)
		    putchar ('{');
		  fwrite (buffer, buflen, 1, stdout);
		  if (closing_brace)
		    putchar ('}');
		}
	    }
	  else
	    {
	      do_ungetc (c);
	      putchar ('$');
	      if (opening_brace)
		putchar ('{');
	    }
	}
      else
	putchar (c);
    }
}
