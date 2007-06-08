/*
 * Another stupid program, this one parsing the headers of an
 * email to figure out authorship and subject
 */
#include "cache.h"
#include "builtin.h"
#include "utf8.h"

static FILE *cmitmsg, *patchfile, *fin, *fout;

static int keep_subject;
static const char *metainfo_charset;
static char line[1000];
static char name[1000];
static char email[1000];

static enum  {
	TE_DONTCARE, TE_QP, TE_BASE64,
} transfer_encoding;
static enum  {
	TYPE_TEXT, TYPE_OTHER,
} message_type;

static char charset[256];
static int patch_lines;
static char **p_hdr_data, **s_hdr_data;

#define MAX_HDR_PARSED 10
#define MAX_BOUNDARIES 5

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

static int handle_header(char *line, char *data, int ofs)
{
	if (!line || !data)
		return 1;

	strcpy(data, line+ofs);

	return 0;
}

/* NOTE NOTE NOTE.  We do not claim we do full MIME.  We just attempt
 * to have enough heuristics to grok MIME encoded patches often found
 * on our mailing lists.  For example, we do not even treat header lines
 * case insensitively.
 */

static int slurp_attr(const char *line, const char *name, char *attr)
{
	const char *ends, *ap = strcasestr(line, name);
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

struct content_type {
	char *boundary;
	int boundary_len;
};

static struct content_type content[MAX_BOUNDARIES];

static struct content_type *content_top = content;

static int handle_content_type(char *line)
{
	char boundary[256];

	if (strcasestr(line, "text/") == NULL)
		 message_type = TYPE_OTHER;
	if (slurp_attr(line, "boundary=", boundary + 2)) {
		memcpy(boundary, "--", 2);
		if (content_top++ >= &content[MAX_BOUNDARIES]) {
			fprintf(stderr, "Too many boundaries to handle\n");
			exit(1);
		}
		content_top->boundary_len = strlen(boundary);
		content_top->boundary = xmalloc(content_top->boundary_len+1);
		strcpy(content_top->boundary, boundary);
	}
	if (slurp_attr(line, "charset=", charset)) {
		int i, c;
		for (i = 0; (c = charset[i]) != 0; i++)
			charset[i] = tolower(c);
	}
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
	return (!memcmp(line, content_top->boundary, content_top->boundary_len));
}

static int eatspace(char *line)
{
	int len = strlen(line);
	while (len > 0 && isspace(line[len-1]))
		line[--len] = 0;
	return len;
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
				subject += 3;
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
		eatspace(subject);
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

static void decode_header(char *it);
static char *header[MAX_HDR_PARSED] = {
	"From","Subject","Date",
};

static int check_header(char *line, char **hdr_data, int overwrite)
{
	int i;

	/* search for the interesting parts */
	for (i = 0; header[i]; i++) {
		int len = strlen(header[i]);
		if ((!hdr_data[i] || overwrite) &&
		    !strncasecmp(line, header[i], len) &&
		    line[len] == ':' && isspace(line[len + 1])) {
			/* Unwrap inline B and Q encoding, and optionally
			 * normalize the meta information to utf8.
			 */
			decode_header(line + len + 2);
			hdr_data[i] = xmalloc(1000 * sizeof(char));
			if (! handle_header(line, hdr_data[i], len + 2)) {
				return 1;
			}
		}
	}

	/* Content stuff */
	if (!strncasecmp(line, "Content-Type", 12) &&
		line[12] == ':' && isspace(line[12 + 1])) {
		decode_header(line + 12 + 2);
		if (! handle_content_type(line)) {
			return 1;
		}
	}
	if (!strncasecmp(line, "Content-Transfer-Encoding", 25) &&
		line[25] == ':' && isspace(line[25 + 1])) {
		decode_header(line + 25 + 2);
		if (! handle_content_transfer_encoding(line)) {
			return 1;
		}
	}

	/* for inbody stuff */
	if (!memcmp(">From", line, 5) && isspace(line[5]))
		return 1;
	if (!memcmp("[PATCH]", line, 7) && isspace(line[7])) {
		for (i = 0; header[i]; i++) {
			if (!memcmp("Subject: ", header[i], 9)) {
				if (! handle_header(line, hdr_data[i], 0)) {
					return 1;
				}
			}
		}
	}

	/* no match */
	return 0;
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

	/* Count mbox From headers as headers */
	if (!memcmp(line, "From ", 5) || !memcmp(line, ">From ", 6))
		return 1;

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

/*
 * sz is size of 'line' buffer in bytes.  Must be reasonably
 * long enough to hold one physical real-world e-mail line.
 */
static int read_one_header_line(char *line, int sz, FILE *in)
{
	int len;

	/*
	 * We will read at most (sz-1) bytes and then potentially
	 * re-add NUL after it.  Accessing line[sz] after this is safe
	 * and we can allow len to grow up to and including sz.
	 */
	sz--;

	/* Get the first part of the line. */
	if (!fgets(line, sz, in))
		return 0;

	/*
	 * Is it an empty line or not a valid rfc2822 header?
	 * If so, stop here, and return false ("not a header")
	 */
	len = eatspace(line);
	if (!len || !is_rfc2822_header(line)) {
		/* Re-add the newline */
		line[len] = '\n';
		line[len + 1] = '\0';
		return 0;
	}

	/*
	 * Now we need to eat all the continuation lines..
	 * Yuck, 2822 header "folding"
	 */
	for (;;) {
		int peek, addlen;
		static char continuation[1000];

		peek = fgetc(in); ungetc(peek, in);
		if (peek != ' ' && peek != '\t')
			break;
		if (!fgets(continuation, sizeof(continuation), in))
			break;
		addlen = eatspace(continuation);
		if (len < sz - 1) {
			if (addlen >= sz - len)
				addlen = sz - len - 1;
			memcpy(line + len, continuation, addlen);
			len += addlen;
		}
	}
	line[len] = 0;

	return 1;
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

static void convert_to_utf8(char *line, const char *charset)
{
	static const char latin_one[] = "latin1";
	const char *input_charset = *charset ? charset : latin_one;
	char *out = reencode_string(line, metainfo_charset, input_charset);

	if (!out)
		die("cannot convert from %s to %s\n",
		    input_charset, metainfo_charset);
	strcpy(line, out);
	free(out);
}

static int decode_header_bq(char *it)
{
	char *in, *out, *ep, *cp, *sp;
	char outbuf[1000];
	int rfc2047 = 0;

	in = it;
	out = outbuf;
	while ((ep = strstr(in, "=?")) != NULL) {
		int sz, encoding;
		char charset_q[256], piecebuf[256];
		rfc2047 = 1;

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
			return rfc2047; /* no munging */
		for (sp = ep; sp < cp; sp++)
			charset_q[sp - ep] = tolower(*sp);
		charset_q[cp - ep] = 0;
		encoding = cp[1];
		if (!encoding || cp[2] != '?')
			return rfc2047; /* no munging */
		ep = strstr(cp + 3, "?=");
		if (!ep)
			return rfc2047; /* no munging */
		switch (tolower(encoding)) {
		default:
			return rfc2047; /* no munging */
		case 'b':
			sz = decode_b_segment(cp + 3, piecebuf, ep);
			break;
		case 'q':
			sz = decode_q_segment(cp + 3, piecebuf, ep, 1);
			break;
		}
		if (sz < 0)
			return rfc2047;
		if (metainfo_charset)
			convert_to_utf8(piecebuf, charset_q);
		strcpy(out, piecebuf);
		out += strlen(out);
		in = ep + 2;
	}
	strcpy(out, in);
	strcpy(it, outbuf);
	return rfc2047;
}

static void decode_header(char *it)
{

	if (decode_header_bq(it))
		return;
	/* otherwise "it" is a straight copy of the input.
	 * This can be binary guck but there is no charset specified.
	 */
	if (metainfo_charset)
		convert_to_utf8(it, "");
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

static int handle_filter(char *line);

static int find_boundary(void)
{
	while(fgets(line, sizeof(line), fin) != NULL) {
		if (is_multipart_boundary(line))
			return 1;
	}
	return 0;
}

static int handle_boundary(void)
{
	char newline[]="\n";
again:
	if (!memcmp(line+content_top->boundary_len, "--", 2)) {
		/* we hit an end boundary */
		/* pop the current boundary off the stack */
		free(content_top->boundary);

		/* technically won't happen as is_multipart_boundary()
		   will fail first.  But just in case..
		 */
		if (content_top-- < content) {
			fprintf(stderr, "Detected mismatched boundaries, "
					"can't recover\n");
			exit(1);
		}
		handle_filter(newline);

		/* skip to the next boundary */
		if (!find_boundary())
			return 0;
		goto again;
	}

	/* set some defaults */
	transfer_encoding = TE_DONTCARE;
	charset[0] = 0;
	message_type = TYPE_TEXT;

	/* slurp in this section's info */
	while (read_one_header_line(line, sizeof(line), fin))
		check_header(line, p_hdr_data, 0);

	/* eat the blank line after section info */
	return (fgets(line, sizeof(line), fin) != NULL);
}

static inline int patchbreak(const char *line)
{
	/* Beginning of a "diff -" header? */
	if (!memcmp("diff -", line, 6))
		return 1;

	/* CVS "Index: " line? */
	if (!memcmp("Index: ", line, 7))
		return 1;

	/*
	 * "--- <filename>" starts patches without headers
	 * "---<sp>*" is a manual separator
	 */
	if (!memcmp("---", line, 3)) {
		line += 3;
		/* space followed by a filename? */
		if (line[0] == ' ' && !isspace(line[1]))
			return 1;
		/* Just whitespace? */
		for (;;) {
			unsigned char c = *line++;
			if (c == '\n')
				return 1;
			if (!isspace(c))
				break;
		}
		return 0;
	}
	return 0;
}


static int handle_commit_msg(char *line)
{
	static int still_looking = 1;

	if (!cmitmsg)
		return 0;

	if (still_looking) {
		char *cp = line;
		if (isspace(*line)) {
			for (cp = line + 1; *cp; cp++) {
				if (!isspace(*cp))
					break;
			}
			if (!*cp)
				return 0;
		}
		if ((still_looking = check_header(cp, s_hdr_data, 0)) != 0)
			return 0;
	}

	/* normalize the log message to UTF-8. */
	if (metainfo_charset)
		convert_to_utf8(line, charset);

	if (patchbreak(line)) {
		fclose(cmitmsg);
		cmitmsg = NULL;
		return 1;
	}

	fputs(line, cmitmsg);
	return 0;
}

static int handle_patch(char *line)
{
	fputs(line, patchfile);
	patch_lines++;
	return 0;
}

static int handle_filter(char *line)
{
	static int filter = 0;

	/* filter tells us which part we left off on
	 * a non-zero return indicates we hit a filter point
	 */
	switch (filter) {
	case 0:
		if (!handle_commit_msg(line))
			break;
		filter++;
	case 1:
		if (!handle_patch(line))
			break;
		filter++;
	default:
		return 1;
	}

	return 0;
}

static void handle_body(void)
{
	int rc = 0;
	static char newline[2000];
	static char *np = newline;

	/* Skip up to the first boundary */
	if (content_top->boundary) {
		if (!find_boundary())
			return;
	}

	do {
		/* process any boundary lines */
		if (content_top->boundary && is_multipart_boundary(line)) {
			/* flush any leftover */
			if ((transfer_encoding == TE_BASE64)  &&
			    (np != newline)) {
				handle_filter(newline);
			}
			if (!handle_boundary())
				return;
		}

		/* Unwrap transfer encoding */
		decode_transfer_encoding(line);

		switch (transfer_encoding) {
		case TE_BASE64:
		{
			char *op = line;

			/* binary data most likely doesn't have newlines */
			if (message_type != TYPE_TEXT) {
				rc = handle_filter(line);
				break;
			}

			/* this is a decoded line that may contain
			 * multiple new lines.  Pass only one chunk
			 * at a time to handle_filter()
			 */

			do {
				while (*op != '\n' && *op != 0)
					*np++ = *op++;
				*np = *op;
				if (*np != 0) {
					/* should be sitting on a new line */
					*(++np) = 0;
					op++;
					rc = handle_filter(newline);
					np = newline;
				}
			} while (*op != 0);
			/* the partial chunk is saved in newline and
			 * will be appended by the next iteration of fgets
			 */
			break;
		}
		default:
			rc = handle_filter(line);
		}
		if (rc)
			/* nothing left to filter */
			break;
	} while (fgets(line, sizeof(line), fin));

	return;
}

static void handle_info(void)
{
	char *sub;
	char *hdr;
	int i;

	for (i = 0; header[i]; i++) {

		/* only print inbody headers if we output a patch file */
		if (patch_lines && s_hdr_data[i])
			hdr = s_hdr_data[i];
		else if (p_hdr_data[i])
			hdr = p_hdr_data[i];
		else
			continue;

		if (!memcmp(header[i], "Subject", 7)) {
			sub = cleanup_subject(hdr);
			cleanup_space(sub);
			fprintf(fout, "Subject: %s\n", sub);
		} else if (!memcmp(header[i], "From", 4)) {
			handle_from(hdr);
			fprintf(fout, "Author: %s\n", name);
			fprintf(fout, "Email: %s\n", email);
		} else {
			cleanup_space(hdr);
			fprintf(fout, "%s: %s\n", header[i], hdr);
		}
	}
	fprintf(fout, "\n");
}

static int mailinfo(FILE *in, FILE *out, int ks, const char *encoding,
		    const char *msg, const char *patch)
{
	keep_subject = ks;
	metainfo_charset = encoding;
	fin = in;
	fout = out;

	cmitmsg = fopen(msg, "w");
	if (!cmitmsg) {
		perror(msg);
		return -1;
	}
	patchfile = fopen(patch, "w");
	if (!patchfile) {
		perror(patch);
		fclose(cmitmsg);
		return -1;
	}

	p_hdr_data = xcalloc(MAX_HDR_PARSED, sizeof(char *));
	s_hdr_data = xcalloc(MAX_HDR_PARSED, sizeof(char *));

	/* process the email header */
	while (read_one_header_line(line, sizeof(line), fin))
		check_header(line, p_hdr_data, 1);

	handle_body();
	handle_info();

	return 0;
}

static const char mailinfo_usage[] =
	"git-mailinfo [-k] [-u | --encoding=<encoding>] msg patch <mail >info";

int cmd_mailinfo(int argc, const char **argv, const char *prefix)
{
	const char *def_charset;

	/* NEEDSWORK: might want to do the optional .git/ directory
	 * discovery
	 */
	git_config(git_default_config);

	def_charset = (git_commit_encoding ? git_commit_encoding : "utf-8");
	metainfo_charset = def_charset;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-k"))
			keep_subject = 1;
		else if (!strcmp(argv[1], "-u"))
			metainfo_charset = def_charset;
		else if (!strcmp(argv[1], "-n"))
			metainfo_charset = NULL;
		else if (!prefixcmp(argv[1], "--encoding="))
			metainfo_charset = argv[1] + 11;
		else
			usage(mailinfo_usage);
		argc--; argv++;
	}

	if (argc != 3)
		usage(mailinfo_usage);

	return !!mailinfo(stdin, stdout, keep_subject, metainfo_charset, argv[1], argv[2]);
}
