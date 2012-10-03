/*
 * Another stupid program, this one parsing the headers of an
 * email to figure out authorship and subject
 */
#include "cache.h"
#include "builtin.h"
#include "utf8.h"
#include "strbuf.h"

static FILE *cmitmsg, *patchfile, *fin, *fout;

static int keep_subject;
static int keep_non_patch_brackets_in_subject;
static const char *metainfo_charset;
static struct strbuf line = STRBUF_INIT;
static struct strbuf name = STRBUF_INIT;
static struct strbuf email = STRBUF_INIT;

static enum  {
	TE_DONTCARE, TE_QP, TE_BASE64
} transfer_encoding;

static struct strbuf charset = STRBUF_INIT;
static int patch_lines;
static struct strbuf **p_hdr_data, **s_hdr_data;
static int use_scissors;
static int use_inbody_headers = 1;

#define MAX_HDR_PARSED 10
#define MAX_BOUNDARIES 5

static void cleanup_space(struct strbuf *sb);


static void get_sane_name(struct strbuf *out, struct strbuf *name, struct strbuf *email)
{
	struct strbuf *src = name;
	if (name->len < 3 || 60 < name->len || strchr(name->buf, '@') ||
		strchr(name->buf, '<') || strchr(name->buf, '>'))
		src = email;
	else if (name == out)
		return;
	strbuf_reset(out);
	strbuf_addbuf(out, src);
}

static void parse_bogus_from(const struct strbuf *line)
{
	/* John Doe <johndoe> */

	char *bra, *ket;
	/* This is fallback, so do not bother if we already have an
	 * e-mail address.
	 */
	if (email.len)
		return;

	bra = strchr(line->buf, '<');
	if (!bra)
		return;
	ket = strchr(bra, '>');
	if (!ket)
		return;

	strbuf_reset(&email);
	strbuf_add(&email, bra + 1, ket - bra - 1);

	strbuf_reset(&name);
	strbuf_add(&name, line->buf, bra - line->buf);
	strbuf_trim(&name);
	get_sane_name(&name, &name, &email);
}

static void handle_from(const struct strbuf *from)
{
	char *at;
	size_t el;
	struct strbuf f;

	strbuf_init(&f, from->len);
	strbuf_addbuf(&f, from);

	at = strchr(f.buf, '@');
	if (!at) {
		parse_bogus_from(from);
		return;
	}

	/*
	 * If we already have one email, don't take any confusing lines
	 */
	if (email.len && strchr(at + 1, '@')) {
		strbuf_release(&f);
		return;
	}

	/* Pick up the string around '@', possibly delimited with <>
	 * pair; that is the email part.
	 */
	while (at > f.buf) {
		char c = at[-1];
		if (isspace(c))
			break;
		if (c == '<') {
			at[-1] = ' ';
			break;
		}
		at--;
	}
	el = strcspn(at, " \n\t\r\v\f>");
	strbuf_reset(&email);
	strbuf_add(&email, at, el);
	strbuf_remove(&f, at - f.buf, el + (at[el] ? 1 : 0));

	/* The remainder is name.  It could be
	 *
	 * - "John Doe <john.doe@xz>"			(a), or
	 * - "john.doe@xz (John Doe)"			(b), or
	 * - "John (zzz) Doe <john.doe@xz> (Comment)"	(c)
	 *
	 * but we have removed the email part, so
	 *
	 * - remove extra spaces which could stay after email (case 'c'), and
	 * - trim from both ends, possibly removing the () pair at the end
	 *   (cases 'a' and 'b').
	 */
	cleanup_space(&f);
	strbuf_trim(&f);
	if (f.buf[0] == '(' && f.len && f.buf[f.len - 1] == ')') {
		strbuf_remove(&f, 0, 1);
		strbuf_setlen(&f, f.len - 1);
	}

	get_sane_name(&name, &f, &email);
	strbuf_release(&f);
}

static void handle_header(struct strbuf **out, const struct strbuf *line)
{
	if (!*out) {
		*out = xmalloc(sizeof(struct strbuf));
		strbuf_init(*out, line->len);
	} else
		strbuf_reset(*out);

	strbuf_addbuf(*out, line);
}

/* NOTE NOTE NOTE.  We do not claim we do full MIME.  We just attempt
 * to have enough heuristics to grok MIME encoded patches often found
 * on our mailing lists.  For example, we do not even treat header lines
 * case insensitively.
 */

static int slurp_attr(const char *line, const char *name, struct strbuf *attr)
{
	const char *ends, *ap = strcasestr(line, name);
	size_t sz;

	strbuf_setlen(attr, 0);
	if (!ap)
		return 0;
	ap += strlen(name);
	if (*ap == '"') {
		ap++;
		ends = "\"";
	}
	else
		ends = "; \t";
	sz = strcspn(ap, ends);
	strbuf_add(attr, ap, sz);
	return 1;
}

static struct strbuf *content[MAX_BOUNDARIES];

static struct strbuf **content_top = content;

static void handle_content_type(struct strbuf *line)
{
	struct strbuf *boundary = xmalloc(sizeof(struct strbuf));
	strbuf_init(boundary, line->len);

	if (slurp_attr(line->buf, "boundary=", boundary)) {
		strbuf_insert(boundary, 0, "--", 2);
		if (++content_top > &content[MAX_BOUNDARIES]) {
			fprintf(stderr, "Too many boundaries to handle\n");
			exit(1);
		}
		*content_top = boundary;
		boundary = NULL;
	}
	slurp_attr(line->buf, "charset=", &charset);

	if (boundary) {
		strbuf_release(boundary);
		free(boundary);
	}
}

static void handle_content_transfer_encoding(const struct strbuf *line)
{
	if (strcasestr(line->buf, "base64"))
		transfer_encoding = TE_BASE64;
	else if (strcasestr(line->buf, "quoted-printable"))
		transfer_encoding = TE_QP;
	else
		transfer_encoding = TE_DONTCARE;
}

static int is_multipart_boundary(const struct strbuf *line)
{
	return (((*content_top)->len <= line->len) &&
		!memcmp(line->buf, (*content_top)->buf, (*content_top)->len));
}

static void cleanup_subject(struct strbuf *subject)
{
	size_t at = 0;

	while (at < subject->len) {
		char *pos;
		size_t remove;

		switch (subject->buf[at]) {
		case 'r': case 'R':
			if (subject->len <= at + 3)
				break;
			if ((subject->buf[at + 1] == 'e' ||
			     subject->buf[at + 1] == 'E') &&
			    subject->buf[at + 2] == ':') {
				strbuf_remove(subject, at, 3);
				continue;
			}
			at++;
			break;
		case ' ': case '\t': case ':':
			strbuf_remove(subject, at, 1);
			continue;
		case '[':
			pos = strchr(subject->buf + at, ']');
			if (!pos)
				break;
			remove = pos - subject->buf + at + 1;
			if (!keep_non_patch_brackets_in_subject ||
			    (7 <= remove &&
			     memmem(subject->buf + at, remove, "PATCH", 5)))
				strbuf_remove(subject, at, remove);
			else {
				at += remove;
				/*
				 * If the input had a space after the ], keep
				 * it.  We don't bother with finding the end of
				 * the space, since we later normalize it
				 * anyway.
				 */
				if (isspace(subject->buf[at]))
					at += 1;
			}
			continue;
		}
		break;
	}
	strbuf_trim(subject);
}

static void cleanup_space(struct strbuf *sb)
{
	size_t pos, cnt;
	for (pos = 0; pos < sb->len; pos++) {
		if (isspace(sb->buf[pos])) {
			sb->buf[pos] = ' ';
			for (cnt = 0; isspace(sb->buf[pos + cnt + 1]); cnt++);
			strbuf_remove(sb, pos + 1, cnt);
		}
	}
}

static void decode_header(struct strbuf *line);
static const char *header[MAX_HDR_PARSED] = {
	"From","Subject","Date",
};

static inline int cmp_header(const struct strbuf *line, const char *hdr)
{
	int len = strlen(hdr);
	return !strncasecmp(line->buf, hdr, len) && line->len > len &&
			line->buf[len] == ':' && isspace(line->buf[len + 1]);
}

static int check_header(const struct strbuf *line,
				struct strbuf *hdr_data[], int overwrite)
{
	int i, ret = 0, len;
	struct strbuf sb = STRBUF_INIT;
	/* search for the interesting parts */
	for (i = 0; header[i]; i++) {
		int len = strlen(header[i]);
		if ((!hdr_data[i] || overwrite) && cmp_header(line, header[i])) {
			/* Unwrap inline B and Q encoding, and optionally
			 * normalize the meta information to utf8.
			 */
			strbuf_add(&sb, line->buf + len + 2, line->len - len - 2);
			decode_header(&sb);
			handle_header(&hdr_data[i], &sb);
			ret = 1;
			goto check_header_out;
		}
	}

	/* Content stuff */
	if (cmp_header(line, "Content-Type")) {
		len = strlen("Content-Type: ");
		strbuf_add(&sb, line->buf + len, line->len - len);
		decode_header(&sb);
		strbuf_insert(&sb, 0, "Content-Type: ", len);
		handle_content_type(&sb);
		ret = 1;
		goto check_header_out;
	}
	if (cmp_header(line, "Content-Transfer-Encoding")) {
		len = strlen("Content-Transfer-Encoding: ");
		strbuf_add(&sb, line->buf + len, line->len - len);
		decode_header(&sb);
		handle_content_transfer_encoding(&sb);
		ret = 1;
		goto check_header_out;
	}

	/* for inbody stuff */
	if (!prefixcmp(line->buf, ">From") && isspace(line->buf[5])) {
		ret = 1; /* Should this return 0? */
		goto check_header_out;
	}
	if (!prefixcmp(line->buf, "[PATCH]") && isspace(line->buf[7])) {
		for (i = 0; header[i]; i++) {
			if (!memcmp("Subject", header[i], 7)) {
				handle_header(&hdr_data[i], line);
				ret = 1;
				goto check_header_out;
			}
		}
	}

check_header_out:
	strbuf_release(&sb);
	return ret;
}

static int is_rfc2822_header(const struct strbuf *line)
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
	char *cp = line->buf;

	/* Count mbox From headers as headers */
	if (!prefixcmp(cp, "From ") || !prefixcmp(cp, ">From "))
		return 1;

	while ((ch = *cp++)) {
		if (ch == ':')
			return 1;
		if ((33 <= ch && ch <= 57) ||
		    (59 <= ch && ch <= 126))
			continue;
		break;
	}
	return 0;
}

static int read_one_header_line(struct strbuf *line, FILE *in)
{
	/* Get the first part of the line. */
	if (strbuf_getline(line, in, '\n'))
		return 0;

	/*
	 * Is it an empty line or not a valid rfc2822 header?
	 * If so, stop here, and return false ("not a header")
	 */
	strbuf_rtrim(line);
	if (!line->len || !is_rfc2822_header(line)) {
		/* Re-add the newline */
		strbuf_addch(line, '\n');
		return 0;
	}

	/*
	 * Now we need to eat all the continuation lines..
	 * Yuck, 2822 header "folding"
	 */
	for (;;) {
		int peek;
		struct strbuf continuation = STRBUF_INIT;

		peek = fgetc(in); ungetc(peek, in);
		if (peek != ' ' && peek != '\t')
			break;
		if (strbuf_getline(&continuation, in, '\n'))
			break;
		continuation.buf[0] = ' ';
		strbuf_rtrim(&continuation);
		strbuf_addbuf(line, &continuation);
	}

	return 1;
}

static struct strbuf *decode_q_segment(const struct strbuf *q_seg, int rfc2047)
{
	const char *in = q_seg->buf;
	int c;
	struct strbuf *out = xmalloc(sizeof(struct strbuf));
	strbuf_init(out, q_seg->len);

	while ((c = *in++) != 0) {
		if (c == '=') {
			int d = *in++;
			if (d == '\n' || !d)
				break; /* drop trailing newline */
			strbuf_addch(out, (hexval(d) << 4) | hexval(*in++));
			continue;
		}
		if (rfc2047 && c == '_') /* rfc2047 4.2 (2) */
			c = 0x20;
		strbuf_addch(out, c);
	}
	return out;
}

static struct strbuf *decode_b_segment(const struct strbuf *b_seg)
{
	/* Decode in..ep, possibly in-place to ot */
	int c, pos = 0, acc = 0;
	const char *in = b_seg->buf;
	struct strbuf *out = xmalloc(sizeof(struct strbuf));
	strbuf_init(out, b_seg->len);

	while ((c = *in++) != 0) {
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
		else
			continue; /* garbage */
		switch (pos++) {
		case 0:
			acc = (c << 2);
			break;
		case 1:
			strbuf_addch(out, (acc | (c >> 4)));
			acc = (c & 15) << 4;
			break;
		case 2:
			strbuf_addch(out, (acc | (c >> 2)));
			acc = (c & 3) << 6;
			break;
		case 3:
			strbuf_addch(out, (acc | c));
			acc = pos = 0;
			break;
		}
	}
	return out;
}

static void convert_to_utf8(struct strbuf *line, const char *charset)
{
	char *out;

	if (!charset || !*charset)
		return;
	if (!strcasecmp(metainfo_charset, charset))
		return;
	out = reencode_string(line->buf, metainfo_charset, charset);
	if (!out)
		die("cannot convert from %s to %s",
		    charset, metainfo_charset);
	strbuf_attach(line, out, strlen(out), strlen(out));
}

static int decode_header_bq(struct strbuf *it)
{
	char *in, *ep, *cp;
	struct strbuf outbuf = STRBUF_INIT, *dec;
	struct strbuf charset_q = STRBUF_INIT, piecebuf = STRBUF_INIT;
	int rfc2047 = 0;

	in = it->buf;
	while (in - it->buf <= it->len && (ep = strstr(in, "=?")) != NULL) {
		int encoding;
		strbuf_reset(&charset_q);
		strbuf_reset(&piecebuf);
		rfc2047 = 1;

		if (in != ep) {
			/*
			 * We are about to process an encoded-word
			 * that begins at ep, but there is something
			 * before the encoded word.
			 */
			char *scan;
			for (scan = in; scan < ep; scan++)
				if (!isspace(*scan))
					break;

			if (scan != ep || in == it->buf) {
				/*
				 * We should not lose that "something",
				 * unless we have just processed an
				 * encoded-word, and there is only LWS
				 * before the one we are about to process.
				 */
				strbuf_add(&outbuf, in, ep - in);
			}
		}
		/* E.g.
		 * ep : "=?iso-2022-jp?B?GyR...?= foo"
		 * ep : "=?ISO-8859-1?Q?Foo=FCbar?= baz"
		 */
		ep += 2;

		if (ep - it->buf >= it->len || !(cp = strchr(ep, '?')))
			goto decode_header_bq_out;

		if (cp + 3 - it->buf > it->len)
			goto decode_header_bq_out;
		strbuf_add(&charset_q, ep, cp - ep);

		encoding = cp[1];
		if (!encoding || cp[2] != '?')
			goto decode_header_bq_out;
		ep = strstr(cp + 3, "?=");
		if (!ep)
			goto decode_header_bq_out;
		strbuf_add(&piecebuf, cp + 3, ep - cp - 3);
		switch (tolower(encoding)) {
		default:
			goto decode_header_bq_out;
		case 'b':
			dec = decode_b_segment(&piecebuf);
			break;
		case 'q':
			dec = decode_q_segment(&piecebuf, 1);
			break;
		}
		if (metainfo_charset)
			convert_to_utf8(dec, charset_q.buf);

		strbuf_addbuf(&outbuf, dec);
		strbuf_release(dec);
		free(dec);
		in = ep + 2;
	}
	strbuf_addstr(&outbuf, in);
	strbuf_reset(it);
	strbuf_addbuf(it, &outbuf);
decode_header_bq_out:
	strbuf_release(&outbuf);
	strbuf_release(&charset_q);
	strbuf_release(&piecebuf);
	return rfc2047;
}

static void decode_header(struct strbuf *it)
{
	if (decode_header_bq(it))
		return;
	/* otherwise "it" is a straight copy of the input.
	 * This can be binary guck but there is no charset specified.
	 */
	if (metainfo_charset)
		convert_to_utf8(it, "");
}

static void decode_transfer_encoding(struct strbuf *line)
{
	struct strbuf *ret;

	switch (transfer_encoding) {
	case TE_QP:
		ret = decode_q_segment(line, 0);
		break;
	case TE_BASE64:
		ret = decode_b_segment(line);
		break;
	case TE_DONTCARE:
	default:
		return;
	}
	strbuf_reset(line);
	strbuf_addbuf(line, ret);
	strbuf_release(ret);
	free(ret);
}

static void handle_filter(struct strbuf *line);

static int find_boundary(void)
{
	while (!strbuf_getline(&line, fin, '\n')) {
		if (*content_top && is_multipart_boundary(&line))
			return 1;
	}
	return 0;
}

static int handle_boundary(void)
{
	struct strbuf newline = STRBUF_INIT;

	strbuf_addch(&newline, '\n');
again:
	if (line.len >= (*content_top)->len + 2 &&
	    !memcmp(line.buf + (*content_top)->len, "--", 2)) {
		/* we hit an end boundary */
		/* pop the current boundary off the stack */
		strbuf_release(*content_top);
		free(*content_top);
		*content_top = NULL;

		/* technically won't happen as is_multipart_boundary()
		   will fail first.  But just in case..
		 */
		if (--content_top < content) {
			fprintf(stderr, "Detected mismatched boundaries, "
					"can't recover\n");
			exit(1);
		}
		handle_filter(&newline);
		strbuf_release(&newline);

		/* skip to the next boundary */
		if (!find_boundary())
			return 0;
		goto again;
	}

	/* set some defaults */
	transfer_encoding = TE_DONTCARE;
	strbuf_reset(&charset);

	/* slurp in this section's info */
	while (read_one_header_line(&line, fin))
		check_header(&line, p_hdr_data, 0);

	strbuf_release(&newline);
	/* replenish line */
	if (strbuf_getline(&line, fin, '\n'))
		return 0;
	strbuf_addch(&line, '\n');
	return 1;
}

static inline int patchbreak(const struct strbuf *line)
{
	size_t i;

	/* Beginning of a "diff -" header? */
	if (!prefixcmp(line->buf, "diff -"))
		return 1;

	/* CVS "Index: " line? */
	if (!prefixcmp(line->buf, "Index: "))
		return 1;

	/*
	 * "--- <filename>" starts patches without headers
	 * "---<sp>*" is a manual separator
	 */
	if (line->len < 4)
		return 0;

	if (!prefixcmp(line->buf, "---")) {
		/* space followed by a filename? */
		if (line->buf[3] == ' ' && !isspace(line->buf[4]))
			return 1;
		/* Just whitespace? */
		for (i = 3; i < line->len; i++) {
			unsigned char c = line->buf[i];
			if (c == '\n')
				return 1;
			if (!isspace(c))
				break;
		}
		return 0;
	}
	return 0;
}

static int is_scissors_line(const struct strbuf *line)
{
	size_t i, len = line->len;
	int scissors = 0, gap = 0;
	int first_nonblank = -1;
	int last_nonblank = 0, visible, perforation = 0, in_perforation = 0;
	const char *buf = line->buf;

	for (i = 0; i < len; i++) {
		if (isspace(buf[i])) {
			if (in_perforation) {
				perforation++;
				gap++;
			}
			continue;
		}
		last_nonblank = i;
		if (first_nonblank < 0)
			first_nonblank = i;
		if (buf[i] == '-') {
			in_perforation = 1;
			perforation++;
			continue;
		}
		if (i + 1 < len &&
		    (!memcmp(buf + i, ">8", 2) || !memcmp(buf + i, "8<", 2) ||
		     !memcmp(buf + i, ">%", 2) || !memcmp(buf + i, "%<", 2))) {
			in_perforation = 1;
			perforation += 2;
			scissors += 2;
			i++;
			continue;
		}
		in_perforation = 0;
	}

	/*
	 * The mark must be at least 8 bytes long (e.g. "-- >8 --").
	 * Even though there can be arbitrary cruft on the same line
	 * (e.g. "cut here"), in order to avoid misidentification, the
	 * perforation must occupy more than a third of the visible
	 * width of the line, and dashes and scissors must occupy more
	 * than half of the perforation.
	 */

	visible = last_nonblank - first_nonblank + 1;
	return (scissors && 8 <= visible &&
		visible < perforation * 3 &&
		gap * 2 < perforation);
}

static int handle_commit_msg(struct strbuf *line)
{
	static int still_looking = 1;

	if (!cmitmsg)
		return 0;

	if (still_looking) {
		if (!line->len || (line->len == 1 && line->buf[0] == '\n'))
			return 0;
	}

	if (use_inbody_headers && still_looking) {
		still_looking = check_header(line, s_hdr_data, 0);
		if (still_looking)
			return 0;
	} else
		/* Only trim the first (blank) line of the commit message
		 * when ignoring in-body headers.
		 */
		still_looking = 0;

	/* normalize the log message to UTF-8. */
	if (metainfo_charset)
		convert_to_utf8(line, charset.buf);

	if (use_scissors && is_scissors_line(line)) {
		int i;
		if (fseek(cmitmsg, 0L, SEEK_SET))
			die_errno("Could not rewind output message file");
		if (ftruncate(fileno(cmitmsg), 0))
			die_errno("Could not truncate output message file at scissors");
		still_looking = 1;

		/*
		 * We may have already read "secondary headers"; purge
		 * them to give ourselves a clean restart.
		 */
		for (i = 0; header[i]; i++) {
			if (s_hdr_data[i])
				strbuf_release(s_hdr_data[i]);
			s_hdr_data[i] = NULL;
		}
		return 0;
	}

	if (patchbreak(line)) {
		fclose(cmitmsg);
		cmitmsg = NULL;
		return 1;
	}

	fputs(line->buf, cmitmsg);
	return 0;
}

static void handle_patch(const struct strbuf *line)
{
	fwrite(line->buf, 1, line->len, patchfile);
	patch_lines++;
}

static void handle_filter(struct strbuf *line)
{
	static int filter = 0;

	/* filter tells us which part we left off on */
	switch (filter) {
	case 0:
		if (!handle_commit_msg(line))
			break;
		filter++;
	case 1:
		handle_patch(line);
		break;
	}
}

static void handle_body(void)
{
	struct strbuf prev = STRBUF_INIT;

	/* Skip up to the first boundary */
	if (*content_top) {
		if (!find_boundary())
			goto handle_body_out;
	}

	do {
		/* process any boundary lines */
		if (*content_top && is_multipart_boundary(&line)) {
			/* flush any leftover */
			if (prev.len) {
				handle_filter(&prev);
				strbuf_reset(&prev);
			}
			if (!handle_boundary())
				goto handle_body_out;
		}

		/* Unwrap transfer encoding */
		decode_transfer_encoding(&line);

		switch (transfer_encoding) {
		case TE_BASE64:
		case TE_QP:
		{
			struct strbuf **lines, **it, *sb;

			/* Prepend any previous partial lines */
			strbuf_insert(&line, 0, prev.buf, prev.len);
			strbuf_reset(&prev);

			/*
			 * This is a decoded line that may contain
			 * multiple new lines.  Pass only one chunk
			 * at a time to handle_filter()
			 */
			lines = strbuf_split(&line, '\n');
			for (it = lines; (sb = *it); it++) {
				if (*(it + 1) == NULL) /* The last line */
					if (sb->buf[sb->len - 1] != '\n') {
						/* Partial line, save it for later. */
						strbuf_addbuf(&prev, sb);
						break;
					}
				handle_filter(sb);
			}
			/*
			 * The partial chunk is saved in "prev" and will be
			 * appended by the next iteration of read_line_with_nul().
			 */
			strbuf_list_free(lines);
			break;
		}
		default:
			handle_filter(&line);
		}

	} while (!strbuf_getwholeline(&line, fin, '\n'));

handle_body_out:
	strbuf_release(&prev);
}

static void output_header_lines(FILE *fout, const char *hdr, const struct strbuf *data)
{
	const char *sp = data->buf;
	while (1) {
		char *ep = strchr(sp, '\n');
		int len;
		if (!ep)
			len = strlen(sp);
		else
			len = ep - sp;
		fprintf(fout, "%s: %.*s\n", hdr, len, sp);
		if (!ep)
			break;
		sp = ep + 1;
	}
}

static void handle_info(void)
{
	struct strbuf *hdr;
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
			if (!keep_subject) {
				cleanup_subject(hdr);
				cleanup_space(hdr);
			}
			output_header_lines(fout, "Subject", hdr);
		} else if (!memcmp(header[i], "From", 4)) {
			cleanup_space(hdr);
			handle_from(hdr);
			fprintf(fout, "Author: %s\n", name.buf);
			fprintf(fout, "Email: %s\n", email.buf);
		} else {
			cleanup_space(hdr);
			fprintf(fout, "%s: %s\n", header[i], hdr->buf);
		}
	}
	fprintf(fout, "\n");
}

static int mailinfo(FILE *in, FILE *out, const char *msg, const char *patch)
{
	int peek;
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

	p_hdr_data = xcalloc(MAX_HDR_PARSED, sizeof(*p_hdr_data));
	s_hdr_data = xcalloc(MAX_HDR_PARSED, sizeof(*s_hdr_data));

	do {
		peek = fgetc(in);
	} while (isspace(peek));
	ungetc(peek, in);

	/* process the email header */
	while (read_one_header_line(&line, fin))
		check_header(&line, p_hdr_data, 1);

	handle_body();
	handle_info();

	return 0;
}

static int git_mailinfo_config(const char *var, const char *value, void *unused)
{
	if (prefixcmp(var, "mailinfo."))
		return git_default_config(var, value, unused);
	if (!strcmp(var, "mailinfo.scissors")) {
		use_scissors = git_config_bool(var, value);
		return 0;
	}
	/* perhaps others here */
	return 0;
}

static const char mailinfo_usage[] =
	"git mailinfo [-k|-b] [-u | --encoding=<encoding> | -n] [--scissors | --no-scissors] msg patch < mail >info";

int cmd_mailinfo(int argc, const char **argv, const char *prefix)
{
	const char *def_charset;

	/* NEEDSWORK: might want to do the optional .git/ directory
	 * discovery
	 */
	git_config(git_mailinfo_config, NULL);

	def_charset = get_commit_output_encoding();
	metainfo_charset = def_charset;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-k"))
			keep_subject = 1;
		else if (!strcmp(argv[1], "-b"))
			keep_non_patch_brackets_in_subject = 1;
		else if (!strcmp(argv[1], "-u"))
			metainfo_charset = def_charset;
		else if (!strcmp(argv[1], "-n"))
			metainfo_charset = NULL;
		else if (!prefixcmp(argv[1], "--encoding="))
			metainfo_charset = argv[1] + 11;
		else if (!strcmp(argv[1], "--scissors"))
			use_scissors = 1;
		else if (!strcmp(argv[1], "--no-scissors"))
			use_scissors = 0;
		else if (!strcmp(argv[1], "--no-inbody-headers"))
			use_inbody_headers = 0;
		else
			usage(mailinfo_usage);
		argc--; argv++;
	}

	if (argc != 3)
		usage(mailinfo_usage);

	return !!mailinfo(stdin, stdout, argv[1], argv[2]);
}
