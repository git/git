#ifndef QUOTE_H
#define QUOTE_H

#include <stddef.h>
#include <stdio.h>

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', any exclamation point
 * is replaced with '\!', and the whole thing is enclosed in a
 * single quote pair.
 *
 * For example, if you are passing the result to system() as an
 * argument:
 *
 * sprintf(cmd, "foobar %s %s", sq_quote(arg0), sq_quote(arg1))
 *
 * would be appropriate.  If the system() is going to call ssh to
 * run the command on the other side:
 *
 * sprintf(cmd, "git-diff-tree %s %s", sq_quote(arg0), sq_quote(arg1));
 * sprintf(rcmd, "ssh %s %s", sq_quote(host), sq_quote(cmd));
 *
 * Note that the above examples leak memory!  Remember to free result from
 * sq_quote() in a real application.
 *
 * sq_quote_buf() writes to an existing buffer of specified size; it
 * will return the number of characters that would have been written
 * excluding the final null regardless of the buffer size.
 */

extern char *sq_quote(const char *src);
extern void sq_quote_print(FILE *stream, const char *src);
extern size_t sq_quote_buf(char *dst, size_t n, const char *src);

/* This unwraps what sq_quote() produces in place, but returns
 * NULL if the input does not look like what sq_quote would have
 * produced.
 */
extern char *sq_dequote(char *);

extern int quote_c_style(const char *name, char *outbuf, FILE *outfp,
			 int nodq);
extern char *unquote_c_style(const char *quoted, const char **endp);

extern void write_name_quoted(const char *prefix, int prefix_len,
			      const char *name, int quote, FILE *out);

#endif
