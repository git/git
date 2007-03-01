/* Definitions for data structures and routines for the regular
   expression library, version 0.12.

   Copyright (C) 1985, 1989, 1990, 1991, 1992, 1993 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef __REGEXP_LIBRARY_H__
#define __REGEXP_LIBRARY_H__

/* POSIX says that <sys/types.h> must be included (by the caller) before
   <regex.h>.  */

#ifdef VMS
/* VMS doesn't have `size_t' in <sys/types.h>, even though POSIX says it
   should be there.  */
#include <stddef.h>
#endif


/* The following bits are used to determine the regexp syntax we
   recognize.  The set/not-set meanings are chosen so that Emacs syntax
   remains the value 0.  The bits are given in alphabetical order, and
   the definitions shifted by one from the previous bit; thus, when we
   add or remove a bit, only one other definition need change.  */
typedef unsigned reg_syntax_t;

/* If this bit is not set, then \ inside a bracket expression is literal.
   If set, then such a \ quotes the following character.  */
#define RE_BACKSLASH_ESCAPE_IN_LISTS (1)

/* If this bit is not set, then + and ? are operators, and \+ and \? are
     literals. 
   If set, then \+ and \? are operators and + and ? are literals.  */
#define RE_BK_PLUS_QM (RE_BACKSLASH_ESCAPE_IN_LISTS << 1)

/* If this bit is set, then character classes are supported.  They are:
     [:alpha:], [:upper:], [:lower:],  [:digit:], [:alnum:], [:xdigit:],
     [:space:], [:print:], [:punct:], [:graph:], and [:cntrl:].
   If not set, then character classes are not supported.  */
#define RE_CHAR_CLASSES (RE_BK_PLUS_QM << 1)

/* If this bit is set, then ^ and $ are always anchors (outside bracket
     expressions, of course).
   If this bit is not set, then it depends:
        ^  is an anchor if it is at the beginning of a regular
           expression or after an open-group or an alternation operator;
        $  is an anchor if it is at the end of a regular expression, or
           before a close-group or an alternation operator.  

   This bit could be (re)combined with RE_CONTEXT_INDEP_OPS, because
   POSIX draft 11.2 says that * etc. in leading positions is undefined.
   We already implemented a previous draft which made those constructs
   invalid, though, so we haven't changed the code back.  */
#define RE_CONTEXT_INDEP_ANCHORS (RE_CHAR_CLASSES << 1)

/* If this bit is set, then special characters are always special
     regardless of where they are in the pattern.
   If this bit is not set, then special characters are special only in
     some contexts; otherwise they are ordinary.  Specifically, 
     * + ? and intervals are only special when not after the beginning,
     open-group, or alternation operator.  */
#define RE_CONTEXT_INDEP_OPS (RE_CONTEXT_INDEP_ANCHORS << 1)

/* If this bit is set, then *, +, ?, and { cannot be first in an re or
     immediately after an alternation or begin-group operator.  */
#define RE_CONTEXT_INVALID_OPS (RE_CONTEXT_INDEP_OPS << 1)

/* If this bit is set, then . matches newline.
   If not set, then it doesn't.  */
#define RE_DOT_NEWLINE (RE_CONTEXT_INVALID_OPS << 1)

/* If this bit is set, then . doesn't match NUL.
   If not set, then it does.  */
#define RE_DOT_NOT_NULL (RE_DOT_NEWLINE << 1)

/* If this bit is set, nonmatching lists [^...] do not match newline.
   If not set, they do.  */
#define RE_HAT_LISTS_NOT_NEWLINE (RE_DOT_NOT_NULL << 1)

/* If this bit is set, either \{...\} or {...} defines an
     interval, depending on RE_NO_BK_BRACES. 
   If not set, \{, \}, {, and } are literals.  */
#define RE_INTERVALS (RE_HAT_LISTS_NOT_NEWLINE << 1)

/* If this bit is set, +, ? and | aren't recognized as operators.
   If not set, they are.  */
#define RE_LIMITED_OPS (RE_INTERVALS << 1)

/* If this bit is set, newline is an alternation operator.
   If not set, newline is literal.  */
#define RE_NEWLINE_ALT (RE_LIMITED_OPS << 1)

/* If this bit is set, then `{...}' defines an interval, and \{ and \}
     are literals.
  If not set, then `\{...\}' defines an interval.  */
#define RE_NO_BK_BRACES (RE_NEWLINE_ALT << 1)

/* If this bit is set, (...) defines a group, and \( and \) are literals.
   If not set, \(...\) defines a group, and ( and ) are literals.  */
#define RE_NO_BK_PARENS (RE_NO_BK_BRACES << 1)

/* If this bit is set, then \<digit> matches <digit>.
   If not set, then \<digit> is a back-reference.  */
#define RE_NO_BK_REFS (RE_NO_BK_PARENS << 1)

/* If this bit is set, then | is an alternation operator, and \| is literal. 
   If not set, then \| is an alternation operator, and | is literal.  */
#define RE_NO_BK_VBAR (RE_NO_BK_REFS << 1)

/* If this bit is set, then an ending range point collating higher
     than the starting range point, as in [z-a], is invalid.
   If not set, then when ending range point collates higher than the
     starting range point, the range is ignored.  */
#define RE_NO_EMPTY_RANGES (RE_NO_BK_VBAR << 1)

/* If this bit is set, then an unmatched ) is ordinary.
   If not set, then an unmatched ) is invalid.  */
#define RE_UNMATCHED_RIGHT_PAREN_ORD (RE_NO_EMPTY_RANGES << 1)

/* This global variable defines the particular regexp syntax to use (for
   some interfaces).  When a regexp is compiled, the syntax used is
   stored in the pattern buffer, so changing this does not affect
   already-compiled regexps.  */
extern reg_syntax_t re_syntax_options;

/* Define combinations of the above bits for the standard possibilities.
   (The [[[ comments delimit what gets put into the Texinfo file, so
   don't delete them!)  */ 
/* [[[begin syntaxes]]] */
#define RE_SYNTAX_EMACS 0

#define RE_SYNTAX_AWK							\
  (RE_BACKSLASH_ESCAPE_IN_LISTS | RE_DOT_NOT_NULL			\
   | RE_NO_BK_PARENS            | RE_NO_BK_REFS				\
   | RE_NO_BK_VBAR               | RE_NO_EMPTY_RANGES			\
   | RE_UNMATCHED_RIGHT_PAREN_ORD)

#define RE_SYNTAX_POSIX_AWK 						\
  (RE_SYNTAX_POSIX_EXTENDED | RE_BACKSLASH_ESCAPE_IN_LISTS)

#define RE_SYNTAX_GREP							\
  (RE_BK_PLUS_QM              | RE_CHAR_CLASSES				\
   | RE_HAT_LISTS_NOT_NEWLINE | RE_INTERVALS				\
   | RE_NEWLINE_ALT)

#define RE_SYNTAX_EGREP							\
  (RE_CHAR_CLASSES        | RE_CONTEXT_INDEP_ANCHORS			\
   | RE_CONTEXT_INDEP_OPS | RE_HAT_LISTS_NOT_NEWLINE			\
   | RE_NEWLINE_ALT       | RE_NO_BK_PARENS				\
   | RE_NO_BK_VBAR)

#define RE_SYNTAX_POSIX_EGREP						\
  (RE_SYNTAX_EGREP | RE_INTERVALS | RE_NO_BK_BRACES)

/* P1003.2/D11.2, section 4.20.7.1, lines 5078ff.  */
#define RE_SYNTAX_ED RE_SYNTAX_POSIX_BASIC

#define RE_SYNTAX_SED RE_SYNTAX_POSIX_BASIC

/* Syntax bits common to both basic and extended POSIX regex syntax.  */
#define _RE_SYNTAX_POSIX_COMMON						\
  (RE_CHAR_CLASSES | RE_DOT_NEWLINE      | RE_DOT_NOT_NULL		\
   | RE_INTERVALS  | RE_NO_EMPTY_RANGES)

#define RE_SYNTAX_POSIX_BASIC						\
  (_RE_SYNTAX_POSIX_COMMON | RE_BK_PLUS_QM)

/* Differs from ..._POSIX_BASIC only in that RE_BK_PLUS_QM becomes
   RE_LIMITED_OPS, i.e., \? \+ \| are not recognized.  Actually, this
   isn't minimal, since other operators, such as \`, aren't disabled.  */
#define RE_SYNTAX_POSIX_MINIMAL_BASIC					\
  (_RE_SYNTAX_POSIX_COMMON | RE_LIMITED_OPS)

#define RE_SYNTAX_POSIX_EXTENDED					\
  (_RE_SYNTAX_POSIX_COMMON | RE_CONTEXT_INDEP_ANCHORS			\
   | RE_CONTEXT_INDEP_OPS  | RE_NO_BK_BRACES				\
   | RE_NO_BK_PARENS       | RE_NO_BK_VBAR				\
   | RE_UNMATCHED_RIGHT_PAREN_ORD)

/* Differs from ..._POSIX_EXTENDED in that RE_CONTEXT_INVALID_OPS
   replaces RE_CONTEXT_INDEP_OPS and RE_NO_BK_REFS is added.  */
#define RE_SYNTAX_POSIX_MINIMAL_EXTENDED				\
  (_RE_SYNTAX_POSIX_COMMON  | RE_CONTEXT_INDEP_ANCHORS			\
   | RE_CONTEXT_INVALID_OPS | RE_NO_BK_BRACES				\
   | RE_NO_BK_PARENS        | RE_NO_BK_REFS				\
   | RE_NO_BK_VBAR	    | RE_UNMATCHED_RIGHT_PAREN_ORD)
/* [[[end syntaxes]]] */

/* Maximum number of duplicates an interval can allow.  Some systems
   (erroneously) define this in other header files, but we want our
   value, so remove any previous define.  */
#ifdef RE_DUP_MAX
#undef RE_DUP_MAX
#endif
#define RE_DUP_MAX ((1 << 15) - 1) 


/* POSIX `cflags' bits (i.e., information for `regcomp').  */

/* If this bit is set, then use extended regular expression syntax.
   If not set, then use basic regular expression syntax.  */
#define REG_EXTENDED 1

/* If this bit is set, then ignore case when matching.
   If not set, then case is significant.  */
#define REG_ICASE (REG_EXTENDED << 1)
 
/* If this bit is set, then anchors do not match at newline
     characters in the string.
   If not set, then anchors do match at newlines.  */
#define REG_NEWLINE (REG_ICASE << 1)

/* If this bit is set, then report only success or fail in regexec.
   If not set, then returns differ between not matching and errors.  */
#define REG_NOSUB (REG_NEWLINE << 1)


/* POSIX `eflags' bits (i.e., information for regexec).  */

/* If this bit is set, then the beginning-of-line operator doesn't match
     the beginning of the string (presumably because it's not the
     beginning of a line).
   If not set, then the beginning-of-line operator does match the
     beginning of the string.  */
#define REG_NOTBOL 1

/* Like REG_NOTBOL, except for the end-of-line.  */
#define REG_NOTEOL (1 << 1)


/* If any error codes are removed, changed, or added, update the
   `re_error_msg' table in regex.c.  */
typedef enum
{
  REG_NOERROR = 0,	/* Success.  */
  REG_NOMATCH,		/* Didn't find a match (for regexec).  */

  /* POSIX regcomp return error codes.  (In the order listed in the
     standard.)  */
  REG_BADPAT,		/* Invalid pattern.  */
  REG_ECOLLATE,		/* Not implemented.  */
  REG_ECTYPE,		/* Invalid character class name.  */
  REG_EESCAPE,		/* Trailing backslash.  */
  REG_ESUBREG,		/* Invalid back reference.  */
  REG_EBRACK,		/* Unmatched left bracket.  */
  REG_EPAREN,		/* Parenthesis imbalance.  */ 
  REG_EBRACE,		/* Unmatched \{.  */
  REG_BADBR,		/* Invalid contents of \{\}.  */
  REG_ERANGE,		/* Invalid range end.  */
  REG_ESPACE,		/* Ran out of memory.  */
  REG_BADRPT,		/* No preceding re for repetition op.  */

  /* Error codes we've added.  */
  REG_EEND,		/* Premature end.  */
  REG_ESIZE,		/* Compiled pattern bigger than 2^16 bytes.  */
  REG_ERPAREN		/* Unmatched ) or \); not returned from regcomp.  */
} reg_errcode_t;

/* This data structure represents a compiled pattern.  Before calling
   the pattern compiler, the fields `buffer', `allocated', `fastmap',
   `translate', and `no_sub' can be set.  After the pattern has been
   compiled, the `re_nsub' field is available.  All other fields are
   private to the regex routines.  */

struct re_pattern_buffer
{
/* [[[begin pattern_buffer]]] */
	/* Space that holds the compiled pattern.  It is declared as
          `unsigned char *' because its elements are
           sometimes used as array indexes.  */
  unsigned char *buffer;

	/* Number of bytes to which `buffer' points.  */
  unsigned long allocated;

	/* Number of bytes actually used in `buffer'.  */
  unsigned long used;	

        /* Syntax setting with which the pattern was compiled.  */
  reg_syntax_t syntax;

        /* Pointer to a fastmap, if any, otherwise zero.  re_search uses
           the fastmap, if there is one, to skip over impossible
           starting points for matches.  */
  char *fastmap;

        /* Either a translate table to apply to all characters before
           comparing them, or zero for no translation.  The translation
           is applied to a pattern when it is compiled and to a string
           when it is matched.  */
  char *translate;

	/* Number of subexpressions found by the compiler.  */
  size_t re_nsub;

        /* Zero if this pattern cannot match the empty string, one else.
           Well, in truth it's used only in `re_search_2', to see
           whether or not we should use the fastmap, so we don't set
           this absolutely perfectly; see `re_compile_fastmap' (the
           `duplicate' case).  */
  unsigned can_be_null : 1;

        /* If REGS_UNALLOCATED, allocate space in the `regs' structure
             for `max (RE_NREGS, re_nsub + 1)' groups.
           If REGS_REALLOCATE, reallocate space if necessary.
           If REGS_FIXED, use what's there.  */
#define REGS_UNALLOCATED 0
#define REGS_REALLOCATE 1
#define REGS_FIXED 2
  unsigned regs_allocated : 2;

        /* Set to zero when `regex_compile' compiles a pattern; set to one
           by `re_compile_fastmap' if it updates the fastmap.  */
  unsigned fastmap_accurate : 1;

        /* If set, `re_match_2' does not return information about
           subexpressions.  */
  unsigned no_sub : 1;

        /* If set, a beginning-of-line anchor doesn't match at the
           beginning of the string.  */ 
  unsigned not_bol : 1;

        /* Similarly for an end-of-line anchor.  */
  unsigned not_eol : 1;

        /* If true, an anchor at a newline matches.  */
  unsigned newline_anchor : 1;

/* [[[end pattern_buffer]]] */
};

typedef struct re_pattern_buffer regex_t;


/* search.c (search_buffer) in Emacs needs this one opcode value.  It is
   defined both in `regex.c' and here.  */
#define RE_EXACTN_VALUE 1

/* Type for byte offsets within the string.  POSIX mandates this.  */
typedef int regoff_t;


/* This is the structure we store register match data in.  See
   regex.texinfo for a full description of what registers match.  */
struct re_registers
{
  unsigned num_regs;
  regoff_t *start;
  regoff_t *end;
};


/* If `regs_allocated' is REGS_UNALLOCATED in the pattern buffer,
   `re_match_2' returns information about at least this many registers
   the first time a `regs' structure is passed.  */
#ifndef RE_NREGS
#define RE_NREGS 30
#endif


/* POSIX specification for registers.  Aside from the different names than
   `re_registers', POSIX uses an array of structures, instead of a
   structure of arrays.  */
typedef struct
{
  regoff_t rm_so;  /* Byte offset from string's start to substring's start.  */
  regoff_t rm_eo;  /* Byte offset from string's start to substring's end.  */
} regmatch_t;

/* Declarations for routines.  */

/* To avoid duplicating every routine declaration -- once with a
   prototype (if we are ANSI), and once without (if we aren't) -- we
   use the following macro to declare argument types.  This
   unfortunately clutters up the declarations a bit, but I think it's
   worth it.  */

#if __STDC__

#define _RE_ARGS(args) args

#else /* not __STDC__ */

#define _RE_ARGS(args) ()

#endif /* not __STDC__ */

/* Sets the current default syntax to SYNTAX, and return the old syntax.
   You can also simply assign to the `re_syntax_options' variable.  */
extern reg_syntax_t re_set_syntax _RE_ARGS ((reg_syntax_t syntax));

/* Compile the regular expression PATTERN, with length LENGTH
   and syntax given by the global `re_syntax_options', into the buffer
   BUFFER.  Return NULL if successful, and an error string if not.  */
extern const char *re_compile_pattern
  _RE_ARGS ((const char *pattern, int length,
             struct re_pattern_buffer *buffer));


/* Compile a fastmap for the compiled pattern in BUFFER; used to
   accelerate searches.  Return 0 if successful and -2 if was an
   internal error.  */
extern int re_compile_fastmap _RE_ARGS ((struct re_pattern_buffer *buffer));


/* Search in the string STRING (with length LENGTH) for the pattern
   compiled into BUFFER.  Start searching at position START, for RANGE
   characters.  Return the starting position of the match, -1 for no
   match, or -2 for an internal error.  Also return register
   information in REGS (if REGS and BUFFER->no_sub are nonzero).  */
extern int re_search
  _RE_ARGS ((struct re_pattern_buffer *buffer, const char *string,
            int length, int start, int range, struct re_registers *regs));


/* Like `re_search', but search in the concatenation of STRING1 and
   STRING2.  Also, stop searching at index START + STOP.  */
extern int re_search_2
  _RE_ARGS ((struct re_pattern_buffer *buffer, const char *string1,
             int length1, const char *string2, int length2,
             int start, int range, struct re_registers *regs, int stop));


/* Like `re_search', but return how many characters in STRING the regexp
   in BUFFER matched, starting at position START.  */
extern int re_match
  _RE_ARGS ((struct re_pattern_buffer *buffer, const char *string,
             int length, int start, struct re_registers *regs));


/* Relates to `re_match' as `re_search_2' relates to `re_search'.  */
extern int re_match_2 
  _RE_ARGS ((struct re_pattern_buffer *buffer, const char *string1,
             int length1, const char *string2, int length2,
             int start, struct re_registers *regs, int stop));


/* Set REGS to hold NUM_REGS registers, storing them in STARTS and
   ENDS.  Subsequent matches using BUFFER and REGS will use this memory
   for recording register information.  STARTS and ENDS must be
   allocated with malloc, and must each be at least `NUM_REGS * sizeof
   (regoff_t)' bytes long.

   If NUM_REGS == 0, then subsequent matches should allocate their own
   register data.

   Unless this function is called, the first search or match using
   PATTERN_BUFFER will allocate its own register data, without
   freeing the old data.  */
extern void re_set_registers
  _RE_ARGS ((struct re_pattern_buffer *buffer, struct re_registers *regs,
             unsigned num_regs, regoff_t *starts, regoff_t *ends));

/* 4.2 bsd compatibility.  */
extern char *re_comp _RE_ARGS ((const char *));
extern int re_exec _RE_ARGS ((const char *));

/* POSIX compatibility.  */
extern int regcomp _RE_ARGS ((regex_t *preg, const char *pattern, int cflags));
extern int regexec
  _RE_ARGS ((const regex_t *preg, const char *string, size_t nmatch,
             regmatch_t pmatch[], int eflags));
extern size_t regerror
  _RE_ARGS ((int errcode, const regex_t *preg, char *errbuf,
             size_t errbuf_size));
extern void regfree _RE_ARGS ((regex_t *preg));

#endif /* not __REGEXP_LIBRARY_H__ */

/*
Local variables:
make-backup-files: t
version-control: t
trim-versions-without-asking: nil
End:
*/
