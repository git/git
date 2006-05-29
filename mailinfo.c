/*
 * Another stupid program, this one parsing the headers of an
 * email to figure out authorship and subject
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef NO_ICONV
#include <iconv.h>
#endif
#include "git-compat-util.h"
#include "cache.h"

static FILE *cmitmsg, *patchfile;

static int keep_subject = 0;
static char *metainfo_charset = NULL;
static char line[1000];
static char date[1000];
static char name[1000];
static char email[1000];
static char subject[1000];

static enum  {
	TE_DONTCARE, TE_QP, TE_BASE64,
} transfer_encoding;
static char charset[256];

static char multipart_boundary[1000];
static int multipart_boundary_len;
static int patch_lines = 0;

static char *sanity_check(char *name, char *email)
{
	int len = strlen(name);
	if (len < 3 || len > 60)
		return email;
	if (strchr(name, '@') || strchr(name, '<') || strchr(name, '>'))
		return email;
	return name;
}

static int bogus_from(char *line)
{
	/* John Doe <johndoe> */
	char *bra, *ket, *dst, *cp;

	/* This is fallback, so do not bother if we already have an
	 * e-mail address.
	 */ 
	if (*email)
		return 0;

	bra = strchr(line, '<');
	if (!bra)
		return 0;
	ket = strchr(bra, '>');
	if (!ket)
		return 0;

	for (dst = email, cp = bra+1; cp < ket; )
		*dst++ = *cp++;
	*dst = 0;
	for (cp = line; isspace(*cp); cp++)
		;
	for (bra--; isspace(*bra); bra--)
		*bra = 0;
	cp = sanity_check(cp, email);
	strcpy(name, cp);
	return 1;
}

static int handle_from(char *in_line)
{
	char line[1000];
	char *at;
	char *dst;

	strcpy(line, in_line);
	at = strchr(line, '@');
	if (!at)
		return bogus_from(line);

	/*
	 * If we already have one email, don't take any confusing lines
	 */
	if (*email && strchr(at+1, '@'))
		return 0;

	/* Pick up the string around '@', possibly delimited with <>
	 * pair; that is the email part.  White them out while copying.
	 */
	while (at > line) {
		char c = at[-1];
		if (isspace(c))
			break;
		if (c == '<') {
			at[-1] = ' ';
			break;
		}
		at--;
	}
	dst = email;
	for (;;) {
		unsigned char c = *at;
		if (!c || c == '>' || isspace(c)) {
			if (c == '>')
				*at = ' ';
			break;
		}
		*at++ = ' ';
		*dst++ = c;
	}
	*dst++ = 0;

	/* The remainder is name.  It could be "John Doe <john.doe@xz>"
	 * or "john.doe@xz (John Doe)", but we have whited out the
	 * email part, so trim from both ends, possibly removing
	 * the () pair at the end.
	 */
	at = line + strlen(line);
	while (at > line) {
		unsigned char c = *--at;
		if (!isspace(c)) {
			at[(c == ')') ? 0 : 1] = 0;
			break;
		}
	}

	at = line;
	for (;;) {
		unsigned char c = *at;
		if (!c || !isspace(c)) {
			if (c == '(')
				at++;
			break;
		}
		at++;
	}
	at = sanity_check(at, email);
	strcpy(name, at);
	return 1;
}

static int handle_date(char *line)
{
	strcpy(date, line);
	return 0;
}

static int handle_subject(char *line)
{
	strcpy(subject, line);
	return 0;
}

/* NOTE NOTE NOTE.  We do not claim we do full MIME.  We just attempt
 * to have enough heuristics to grok MIME encoded patches often found
 * on our mailing lists.  For example, we do not even treat header lines
 * case insensitively.
 */

static int slurp_attr(const char *line, const char *name, char *attr)
{
	char *ends, *ap = strcasestr(line, name);
	size_t sz;

	if (!ap) {
		*attr = 0;
		return 0;
	}
	ap += strlen(name);
	if (*ap == '"') {
		ap++;
		ends = "\"";
	}
	else
		ends = "; \t";
	sz = strcspn(ap, ends);
	memcpy(attr, ap, sz);
	attr[sz] = 0;
	return 1;
}

static int handle_subcontent_type(char *line)
{
	/* We do not want to mess with boundary.  Note that we do not
	 * handle nested multipart.
	 */
	if (strcasestr(line, "boundary=")) {
		fprintf(stderr, "Not handling nested multipart message.\n");
		exit(1);
	}
	slurp_attr(line, "charset=", charset);
	if (*charset) {
		int i, c;
		for (i = 0; (c = charset[i]) != 0; i++)
			charset[i] = tolower(c);
	}
	return 0;
}

static int handle_content_type(char *line)
{
	*multipart_boundary = 0;
	if (slurp_attr(line, "boundary=", multipart_boundary + 2)) {
		memcpy(multipart_boundary, "--", 2);
		multipart_boundary_len = strlen(multipart_boundary);
	}
	slurp_attr(line, "charset=", charset);
	return 0;
}

static int handle_content_transfer_encoding(char *line)
{
	if (strcasestr(line, "base64"))
		transfer_encoding = TE_BASE64;
	else if (strcasestr(line, "quoted-printable"))
		transfer_encoding = TE_QP;
	else
		transfer_encoding = TE_DONTCARE;
	return 0;
}

static int is_multipart_boundary(const char *line)
{
	return (!memcmp(line, multipart_boundary, multipart_boundary_len));
}

static int eatspace(char *line)
{
	int len = strlen(line);
	while (len > 0 && isspace(line[len-1]))
		line[--len] = 0;
	return len;
}

#define SEEN_FROM 01
#define SEEN_DATE 02
#define SEEN_SUBJECT 04
#define SEEN_BOGUS_UNIX_FROM 010
#define SEEN_PREFIX  020

/* First lines of body can have From:, Date:, and Subject: */
static void handle_inbody_header(int *seen, char *line)
{
	if (!memcmp(">From", line, 5) && isspace(line[5])) {
		if (!(*seen & SEEN_BOGUS_UNIX_FROM)) {
			*seen |= SEEN_BOGUS_UNIX_FROM;
			return;
		}
	}
	if (!memcmp("From:", line, 5) && isspace(line[5])) {
		if (!(*seen & SEEN_FROM) && handle_from(line+6)) {
			*seen |= SEEN_FROM;
			return;
		}
	}
	if (!memcmp("Date:", line, 5) && isspace(line[5])) {
		if (!(*seen & SEEN_DATE)) {
			handle_date(line+6);
			*seen |= SEEN_DATE;
			return;
		}
	}
	if (!memcmp("Subject:", line, 8) && isspace(line[8])) {
		if (!(*seen & SEEN_SUBJECT)) {
			handle_subject(line+9);
			*seen |= SEEN_SUBJECT;
			return;
		}
	}
	if (!memcmp("[PATCH]", line, 7) && isspace(line[7])) {
		if (!(*seen & SEEN_SUBJECT)) {
			handle_subject(line);
			*seen |= SEEN_SUBJECT;
			return;
		}
	}
	*seen |= SEEN_PREFIX;
}

static char *cleanup_subject(char *subject)
{
	if (keep_subject)
		return subject;
	for (;;) {
		char *p;
		int len, remove;
		switch (*subject) {
		case 'r': case 'R':
			if (!memcmp("e:", subject+1, 2)) {
				subject +=3;
				continue;
			}
			break;
		case ' ': case '\t': case ':':
			subject++;
			continue;

		case '[':
			p = strchr(subject, ']');
			if (!p) {
				subject++;
				continue;
			}
			len = strlen(p);
			remove = p - subject;
			if (remove <= len *2) {
				subject = p+1;
				continue;
			}	
			break;
		}
		return subject;
	}
}			

static void cleanup_space(char *buf)
{
	unsigned char c;
	while ((c = *buf) != 0) {
		buf++;
		if (isspace(c)) {
			buf[-1] = ' ';
			c = *buf;
			while (isspace(c)) {
				int len = strlen(buf);
				memmove(buf, buf+1, len);
				c = *buf;
			}
		}
	}
}

static void decode_header_bq(char *it);
typedef int (*header_fn_t)(char *);
struct header_def {
	const char *name;
	header_fn_t func;
	int namelen;
};

static void check_header(char *line, struct header_def *header)
{
	int i;

	if (header[0].namelen <= 0) {
		for (i = 0; header[i].name; i++)
			header[i].namelen = strlen(header[i].name);
	}
	for (i = 0; header[i].name; i++) {
		int len = header[i].namelen;
		if (!strncasecmp(line, header[i].name, len) &&
		    line[len] == ':' && isspace(line[len + 1])) {
			/* Unwrap inline B and Q encoding, and optionally
			 * normalize the meta information to utf8.
			 */
			decode_header_bq(line + len + 2);
			header[i].func(line + len + 2);
			break;
		}
	}
}

static void check_subheader_line(char *line)
{
	static struct header_def header[] = {
		{ "Content-Type", handle_subcontent_type },
		{ "Content-Transfer-Encoding",
		  handle_content_transfer_encoding },
		{ NULL },
	};
	check_header(line, header);
}
static void check_header_line(char *line)
{
	static struct header_def header[] = {
		{ "From", handle_from },
		{ "Date", handle_date },
		{ "Subject", handle_subject },
		{ "Content-Type", handle_content_type },
		{ "Content-Transfer-Encoding",
		  handle_content_transfer_encoding },
		{ NULL },
	};
	check_header(line, header);
}

static int is_rfc2822_header(char *line)
{
	/*
	 * The section that defines the loosest possible
	 * field name is "3.6.8 Optional fields".
	 *
	 * optional-field = field-name ":" unstructured CRLF
	 * field-name = 1*ftext
	 * ftext = %d33-57 / %59-126
	 */
	int ch;
	char *cp = line;
	while ((ch = *cp++)) {
		if (ch == ':')
			return cp != line;
		if ((33 <= ch && ch <= 57) ||
		    (59 <= ch && ch <= 126))
			continue;
		break;
	}
	return 0;
}

static int read_one_header_line(char *line, int sz, FILE *in)
{
	int ofs = 0;
	while (ofs < sz) {
		int peek, len;
		if (fgets(line + ofs, sz - ofs, in) == NULL)
			break;
		len = eatspace(line + ofs);
		if (len == 0)
			break;
		if (!is_rfc2822_header(line)) {
			/* Re-add the newline */
			line[ofs + len] = '\n';
			line[ofs + len + 1] = '\0';
			break;
		}
		ofs += len;
		/* Yuck, 2822 header "folding" */
		peek = fgetc(in); ungetc(peek, in);
		if (peek != ' ' && peek != '\t')
			break;
	}
	/* Count mbox From headers as headers */
	if (!ofs && !memcmp(line, "From ", 5))
		ofs = 1;
	return ofs;
}

static unsigned hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return ~0;
}

static int decode_q_segment(char *in, char *ot, char *ep, int rfc2047)
{
	int c;
	while ((c = *in++) != 0 && (in <= ep)) {
		if (c == '=') {
			int d = *in++;
			if (d == '\n' || !d)
				break; /* drop trailing newline */
			*ot++ = ((hexval(d) << 4) | hexval(*in++));
			continue;
		}
		if (rfc2047 && c == '_') /* rfc2047 4.2 (2) */
			c = 0x20;
		*ot++ = c;
	}
	*ot = 0;
	return 0;
}

static int decode_b_segment(char *in, char *ot, char *ep)
{
	/* Decode in..ep, possibly in-place to ot */
	int c, pos = 0, acc = 0;

	while ((c = *in++) != 0 && (in <= ep)) {
		if (c == '+')
			c = 62;
		else if (c == '/')
			c = 63;
		else if ('A' <= c && c <= 'Z')
			c -= 'A';
		else if ('a' <= c && c <= 'z')
			c -= 'a' - 26;
		else if ('0' <= c && c <= '9')
			c -= '0' - 52;
		else if (c == '=') {
			/* padding is almost like (c == 0), except we do
			 * not output NUL resulting only from it;
			 * for now we just trust the data.
			 */
			c = 0;
		}
		else
			continue; /* garbage */
		switch (pos++) {
		case 0:
			acc = (c << 2);
			break;
		case 1:
			*ot++ = (acc | (c >> 4));
			acc = (c & 15) << 4;
			break;
		case 2:
			*ot++ = (acc | (c >> 2));
			acc = (c & 3) << 6;
			break;
		case 3:
			*ot++ = (acc | c);
			acc = pos = 0;
			break;
		}
	}
	*ot = 0;
	return 0;
}

static void convert_to_utf8(char *line, char *charset)
{
#ifndef NO_ICONV
	char *in, *out;
	size_t insize, outsize, nrc;
	char outbuf[4096]; /* cheat */
	static char latin_one[] = "latin1";
	char *input_charset = *charset ? charset : latin_one;
	iconv_t conv = iconv_open(metainfo_charset, input_charset);

	if (conv == (iconv_t) -1) {
		static int warned_latin1_once = 0;
		if (input_charset != latin_one) {
			fprintf(stderr, "cannot convert from %s to %s\n",
				input_charset, metainfo_charset);
			*charset = 0;
		}
		else if (!warned_latin1_once) {
			warned_latin1_once = 1;
			fprintf(stderr, "tried to convert from %s to %s, "
				"but your iconv does not work with it.\n",
				input_charset, metainfo_charset);
		}
		return;
	}
	in = line;
	insize = strlen(in);
	out = outbuf;
	outsize = sizeof(outbuf);
	nrc = iconv(conv, &in, &insize, &out, &outsize);
	iconv_close(conv);
	if (nrc == (size_t) -1)
		return;
	*out = 0;
	strcpy(line, outbuf);
#endif
}

static void decode_header_bq(char *it)
{
	char *in, *out, *ep, *cp, *sp;
	char outbuf[1000];

	in = it;
	out = outbuf;
	while ((ep = strstr(in, "=?")) != NULL) {
		int sz, encoding;
		char charset_q[256], piecebuf[256];
		if (in != ep) {
			sz = ep - in;
			memcpy(out, in, sz);
			out += sz;
			in += sz;
		}
		/* E.g.
		 * ep : "=?iso-2022-jp?B?GyR...?= foo"
		 * ep : "=?ISO-8859-1?Q?Foo=FCbar?= baz"
		 */
		ep += 2;
		cp = strchr(ep, '?');
		if (!cp)
			return; /* no munging */
		for (sp = ep; sp < cp; sp++)
			charset_q[sp - ep] = tolower(*sp);
		charset_q[cp - ep] = 0;
		encoding = cp[1];
		if (!encoding || cp[2] != '?')
			return; /* no munging */
		ep = strstr(cp + 3, "?=");
		if (!ep)
			return; /* no munging */
		switch (tolower(encoding)) {
		default:
			return; /* no munging */
		case 'b':
			sz = decode_b_segment(cp + 3, piecebuf, ep);
			break;
		case 'q':
			sz = decode_q_segment(cp + 3, piecebuf, ep, 1);
			break;
		}
		if (sz < 0)
			return;
		if (metainfo_charset)
			convert_to_utf8(piecebuf, charset_q);
		strcpy(out, piecebuf);
		out += strlen(out);
		in = ep + 2;
	}
	strcpy(out, in);
	strcpy(it, outbuf);
}

static void decode_transfer_encoding(char *line)
{
	char *ep;

	switch (transfer_encoding) {
	case TE_QP:
		ep = line + strlen(line);
		decode_q_segment(line, line, ep, 0);
		break;
	case TE_BASE64:
		ep = line + strlen(line);
		decode_b_segment(line, line, ep);
		break;
	case TE_DONTCARE:
		break;
	}
}

static void handle_info(void)
{
	char *sub;

	sub = cleanup_subject(subject);
	cleanup_space(name);
	cleanup_space(date);
	cleanup_space(email);
	cleanup_space(sub);

	printf("Author: %s\nEmail: %s\nSubject: %s\nDate: %s\n\n",
	       name, email, sub, date);
}

/* We are inside message body and have read line[] already.
 * Spit out the commit log.
 */
static int handle_commit_msg(int *seen)
{
	if (!cmitmsg)
		return 0;
	do {
		if (!memcmp("diff -", line, 6) ||
		    !memcmp("---", line, 3) ||
		    !memcmp("Index: ", line, 7))
			break;
		if ((multipart_boundary[0] && is_multipart_boundary(line))) {
			/* We come here when the first part had only
			 * the commit message without any patch.  We
			 * pretend we have not seen this line yet, and
			 * go back to the loop.
			 */
			return 1;
		}

		/* Unwrap transfer encoding and optionally
		 * normalize the log message to UTF-8.
		 */
		decode_transfer_encoding(line);
		if (metainfo_charset)
			convert_to_utf8(line, charset);

		handle_inbody_header(seen, line);
		if (!(*seen & SEEN_PREFIX))
			continue;

		fputs(line, cmitmsg);
	} while (fgets(line, sizeof(line), stdin) != NULL);
	fclose(cmitmsg);
	cmitmsg = NULL;
	return 0;
}

/* We have done the commit message and have the first
 * line of the patch in line[].
 */
static void handle_patch(void)
{
	do {
		if (multipart_boundary[0] && is_multipart_boundary(line))
			break;
		/* Only unwrap transfer encoding but otherwise do not
		 * do anything.  We do *NOT* want UTF-8 conversion
		 * here; we are dealing with the user payload.
		 */
		decode_transfer_encoding(line);
		fputs(line, patchfile);
		patch_lines++;
	} while (fgets(line, sizeof(line), stdin) != NULL);
}

/* multipart boundary and transfer encoding are set up for us, and we
 * are at the end of the sub header.  do equivalent of handle_body up
 * to the next boundary without closing patchfile --- we will expect
 * that the first part to contain commit message and a patch, and
 * handle other parts as pure patches.
 */
static int handle_multipart_one_part(int *seen)
{
	int n = 0;

	while (fgets(line, sizeof(line), stdin) != NULL) {
	again:
		n++;
		if (is_multipart_boundary(line))
			break;
		if (handle_commit_msg(seen))
			goto again;
		handle_patch();
		break;
	}
	if (n == 0)
		return -1;
	return 0;
}

static void handle_multipart_body(void)
{
	int seen = 0;
	int part_num = 0;

	/* Skip up to the first boundary */
	while (fgets(line, sizeof(line), stdin) != NULL)
		if (is_multipart_boundary(line)) {
			part_num = 1;
			break;
		}
	if (!part_num)
		return;
	/* We are on boundary line.  Start slurping the subhead. */
	while (1) {
		int hdr = read_one_header_line(line, sizeof(line), stdin);
		if (!hdr) {
			if (handle_multipart_one_part(&seen) < 0)
				return;
			/* Reset per part headers */
			transfer_encoding = TE_DONTCARE;
			charset[0] = 0;
		}
		else
			check_subheader_line(line);
	}
	fclose(patchfile);
	if (!patch_lines) {
		fprintf(stderr, "No patch found\n");
		exit(1);
	}
}

/* Non multipart message */
static void handle_body(void)
{
	int seen = 0;

	if (line[0] || fgets(line, sizeof(line), stdin) != NULL) {
		handle_commit_msg(&seen);
		handle_patch();
	}
	fclose(patchfile);
	if (!patch_lines) {
		fprintf(stderr, "No patch found\n");
		exit(1);
	}
}

static const char mailinfo_usage[] =
	"git-mailinfo [-k] [-u | --encoding=<encoding>] msg patch <mail >info";

int main(int argc, char **argv)
{
	/* NEEDSWORK: might want to do the optional .git/ directory
	 * discovery
	 */
	git_config(git_default_config);

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-k"))
			keep_subject = 1;
		else if (!strcmp(argv[1], "-u"))
			metainfo_charset = git_commit_encoding;
		else if (!strncmp(argv[1], "--encoding=", 11))
			metainfo_charset = argv[1] + 11;
		else
			usage(mailinfo_usage);
		argc--; argv++;
	}

	if (argc != 3)
		usage(mailinfo_usage);
	cmitmsg = fopen(argv[1], "w");
	if (!cmitmsg) {
		perror(argv[1]);
		exit(1);
	}
	patchfile = fopen(argv[2], "w");
	if (!patchfile) {
		perror(argv[2]);
		exit(1);
	}
	while (1) {
		int hdr = read_one_header_line(line, sizeof(line), stdin);
		if (!hdr) {
			if (multipart_boundary[0])
				handle_multipart_body();
			else
				handle_body();
			handle_info();
			break;
		}
		check_header_line(line);
	}
	return 0;
}
