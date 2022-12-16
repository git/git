#include "cache.h"
#include "config.h"
#include "utf8.h"
#include "strbuf.h"
#include "mailinfo.h"

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

static void get_sane_name(struct strbuf *out, struct strbuf *name, struct strbuf *email)
{
	struct strbuf *src = name;
	if (!name->len || 60 < name->len || strpbrk(name->buf, "@<>"))
		src = email;
	else if (name == out)
		return;
	strbuf_reset(out);
	strbuf_addbuf(out, src);
}

static void parse_bogus_from(struct mailinfo *mi, const struct strbuf *line)
{
	/* John Doe <johndoe> */

	char *bra, *ket;
	/* This is fallback, so do not bother if we already have an
	 * e-mail address.
	 */
	if (mi->email.len)
		return;

	bra = strchr(line->buf, '<');
	if (!bra)
		return;
	ket = strchr(bra, '>');
	if (!ket)
		return;

	strbuf_reset(&mi->email);
	strbuf_add(&mi->email, bra + 1, ket - bra - 1);

	strbuf_reset(&mi->name);
	strbuf_add(&mi->name, line->buf, bra - line->buf);
	strbuf_trim(&mi->name);
	get_sane_name(&mi->name, &mi->name, &mi->email);
}

static const char *unquote_comment(struct strbuf *outbuf, const char *in)
{
	int c;
	int take_next_literally = 0;

	strbuf_addch(outbuf, '(');

	while ((c = *in++) != 0) {
		if (take_next_literally == 1) {
			take_next_literally = 0;
		} else {
			switch (c) {
			case '\\':
				take_next_literally = 1;
				continue;
			case '(':
				in = unquote_comment(outbuf, in);
				continue;
			case ')':
				strbuf_addch(outbuf, ')');
				return in;
			}
		}

		strbuf_addch(outbuf, c);
	}

	return in;
}

static const char *unquote_quoted_string(struct strbuf *outbuf, const char *in)
{
	int c;
	int take_next_literally = 0;

	while ((c = *in++) != 0) {
		if (take_next_literally == 1) {
			take_next_literally = 0;
		} else {
			switch (c) {
			case '\\':
				take_next_literally = 1;
				continue;
			case '"':
				return in;
			}
		}

		strbuf_addch(outbuf, c);
	}

	return in;
}

static void unquote_quoted_pair(struct strbuf *line)
{
	struct strbuf outbuf;
	const char *in = line->buf;
	int c;

	strbuf_init(&outbuf, line->len);

	while ((c = *in++) != 0) {
		switch (c) {
		case '"':
			in = unquote_quoted_string(&outbuf, in);
			continue;
		case '(':
			in = unquote_comment(&outbuf, in);
			continue;
		}

		strbuf_addch(&outbuf, c);
	}

	strbuf_swap(&outbuf, line);
	strbuf_release(&outbuf);

}

static void handle_from(struct mailinfo *mi, const struct strbuf *from)
{
	char *at;
	size_t el;
	struct strbuf f;

	strbuf_init(&f, from->len);
	strbuf_addbuf(&f, from);

	unquote_quoted_pair(&f);

	at = strchr(f.buf, '@');
	if (!at) {
		parse_bogus_from(mi, from);
		goto out;
	}

	/*
	 * If we already have one email, don't take any confusing lines
	 */
	if (mi->email.len && strchr(at + 1, '@'))
		goto out;

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
	strbuf_reset(&mi->email);
	strbuf_add(&mi->email, at, el);
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

	get_sane_name(&mi->name, &f, &mi->email);
out:
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

static int has_attr_value(const char *line, const char *name, const char *value)
{
	struct strbuf sb = STRBUF_INIT;
	int rc = slurp_attr(line, name, &sb) && !strcasecmp(sb.buf, value);
	strbuf_release(&sb);
	return rc;
}

static void handle_content_type(struct mailinfo *mi, struct strbuf *line)
{
	struct strbuf *boundary = xmalloc(sizeof(struct strbuf));
	strbuf_init(boundary, line->len);

	mi->format_flowed = has_attr_value(line->buf, "format=", "flowed");
	mi->delsp = has_attr_value(line->buf, "delsp=", "yes");

	if (slurp_attr(line->buf, "boundary=", boundary)) {
		strbuf_insertstr(boundary, 0, "--");
		if (++mi->content_top >= &mi->content[MAX_BOUNDARIES]) {
			error("Too many boundaries to handle");
			mi->input_error = -1;
			mi->content_top = &mi->content[MAX_BOUNDARIES] - 1;
			return;
		}
		*(mi->content_top) = boundary;
		boundary = NULL;
	}
	slurp_attr(line->buf, "charset=", &mi->charset);

	if (boundary) {
		strbuf_release(boundary);
		free(boundary);
	}
}

static void handle_content_transfer_encoding(struct mailinfo *mi,
					     const struct strbuf *line)
{
	if (strcasestr(line->buf, "base64"))
		mi->transfer_encoding = TE_BASE64;
	else if (strcasestr(line->buf, "quoted-printable"))
		mi->transfer_encoding = TE_QP;
	else
		mi->transfer_encoding = TE_DONTCARE;
}

static int is_multipart_boundary(struct mailinfo *mi, const struct strbuf *line)
{
	struct strbuf *content_top = *(mi->content_top);

	return ((content_top->len <= line->len) &&
		!memcmp(line->buf, content_top->buf, content_top->len));
}

static void cleanup_subject(struct mailinfo *mi, struct strbuf *subject)
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
			remove = pos - (subject->buf + at) + 1;
			if (!mi->keep_non_patch_brackets_in_subject ||
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

#define MAX_HDR_PARSED 10
static const char *header[MAX_HDR_PARSED] = {
	"From","Subject","Date",
};

static inline int skip_header(const struct strbuf *line, const char *hdr,
			      const char **outval)
{
	const char *val;
	if (!skip_iprefix(line->buf, hdr, &val) ||
	    *val++ != ':')
		return 0;
	while (isspace(*val))
		val++;
	*outval = val;
	return 1;
}

static int is_format_patch_separator(const char *line, int len)
{
	static const char SAMPLE[] =
		"From e6807f3efca28b30decfecb1732a56c7db1137ee Mon Sep 17 00:00:00 2001\n";
	const char *cp;

	if (len != strlen(SAMPLE))
		return 0;
	if (!skip_prefix(line, "From ", &cp))
		return 0;
	if (strspn(cp, "0123456789abcdef") != 40)
		return 0;
	cp += 40;
	return !memcmp(SAMPLE + (cp - line), cp, strlen(SAMPLE) - (cp - line));
}

static struct strbuf *decode_q_segment(const struct strbuf *q_seg, int rfc2047)
{
	const char *in = q_seg->buf;
	int c;
	struct strbuf *out = xmalloc(sizeof(struct strbuf));
	strbuf_init(out, q_seg->len);

	while ((c = *in++) != 0) {
		if (c == '=') {
			int ch, d = *in;
			if (d == '\n' || !d)
				break; /* drop trailing newline */
			ch = hex2chr(in);
			if (ch >= 0) {
				strbuf_addch(out, ch);
				in += 2;
				continue;
			}
			/* garbage -- fall through */
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

static int convert_to_utf8(struct mailinfo *mi,
			   struct strbuf *line, const char *charset)
{
	char *out;
	size_t out_len;

	if (!mi->metainfo_charset || !charset || !*charset)
		return 0;

	if (same_encoding(mi->metainfo_charset, charset))
		return 0;
	out = reencode_string_len(line->buf, line->len,
				  mi->metainfo_charset, charset, &out_len);
	if (!out) {
		mi->input_error = -1;
		return error("cannot convert from %s to %s",
			     charset, mi->metainfo_charset);
	}
	strbuf_attach(line, out, out_len, out_len);
	return 0;
}

static void decode_header(struct mailinfo *mi, struct strbuf *it)
{
	char *in, *ep, *cp;
	struct strbuf outbuf = STRBUF_INIT, *dec;
	struct strbuf charset_q = STRBUF_INIT, piecebuf = STRBUF_INIT;
	int found_error = 1; /* pessimism */

	in = it->buf;
	while (in - it->buf <= it->len && (ep = strstr(in, "=?")) != NULL) {
		int encoding;
		strbuf_reset(&charset_q);
		strbuf_reset(&piecebuf);

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
			goto release_return;

		if (cp + 3 - it->buf > it->len)
			goto release_return;
		strbuf_add(&charset_q, ep, cp - ep);

		encoding = cp[1];
		if (!encoding || cp[2] != '?')
			goto release_return;
		ep = strstr(cp + 3, "?=");
		if (!ep)
			goto release_return;
		strbuf_add(&piecebuf, cp + 3, ep - cp - 3);
		switch (tolower(encoding)) {
		default:
			goto release_return;
		case 'b':
			dec = decode_b_segment(&piecebuf);
			break;
		case 'q':
			dec = decode_q_segment(&piecebuf, 1);
			break;
		}
		if (convert_to_utf8(mi, dec, charset_q.buf))
			goto release_return;

		strbuf_addbuf(&outbuf, dec);
		strbuf_release(dec);
		free(dec);
		in = ep + 2;
	}
	strbuf_addstr(&outbuf, in);
	strbuf_reset(it);
	strbuf_addbuf(it, &outbuf);
	found_error = 0;
release_return:
	strbuf_release(&outbuf);
	strbuf_release(&charset_q);
	strbuf_release(&piecebuf);

	if (found_error)
		mi->input_error = -1;
}

/*
 * Returns true if "line" contains a header matching "hdr", in which case "val"
 * will contain the value of the header with any RFC2047 B and Q encoding
 * unwrapped, and optionally normalize the meta information to utf8.
 */
static int parse_header(const struct strbuf *line,
			const char *hdr,
			struct mailinfo *mi,
			struct strbuf *val)
{
	const char *val_str;

	if (!skip_header(line, hdr, &val_str))
		return 0;
	strbuf_addstr(val, val_str);
	decode_header(mi, val);
	return 1;
}

static int check_header(struct mailinfo *mi,
			const struct strbuf *line,
			struct strbuf *hdr_data[], int overwrite)
{
	int i, ret = 0;
	struct strbuf sb = STRBUF_INIT;

	/* search for the interesting parts */
	for (i = 0; header[i]; i++) {
		if ((!hdr_data[i] || overwrite) &&
		    parse_header(line, header[i], mi, &sb)) {
			handle_header(&hdr_data[i], &sb);
			ret = 1;
			goto check_header_out;
		}
	}

	/* Content stuff */
	if (parse_header(line, "Content-Type", mi, &sb)) {
		handle_content_type(mi, &sb);
		ret = 1;
		goto check_header_out;
	}
	if (parse_header(line, "Content-Transfer-Encoding", mi, &sb)) {
		handle_content_transfer_encoding(mi, &sb);
		ret = 1;
		goto check_header_out;
	}
	if (parse_header(line, "Message-ID", mi, &sb)) {
		if (mi->add_message_id)
			mi->message_id = strbuf_detach(&sb, NULL);
		ret = 1;
		goto check_header_out;
	}

check_header_out:
	strbuf_release(&sb);
	return ret;
}

/*
 * Returns 1 if the given line or any line beginning with the given line is an
 * in-body header (that is, check_header will succeed when passed
 * mi->s_hdr_data).
 */
static int is_inbody_header(const struct mailinfo *mi,
			    const struct strbuf *line)
{
	int i;
	const char *val;
	for (i = 0; header[i]; i++)
		if (!mi->s_hdr_data[i] && skip_header(line, header[i], &val))
			return 1;
	return 0;
}

static void decode_transfer_encoding(struct mailinfo *mi, struct strbuf *line)
{
	struct strbuf *ret;

	switch (mi->transfer_encoding) {
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

static inline int patchbreak(const struct strbuf *line)
{
	size_t i;

	/* Beginning of a "diff -" header? */
	if (starts_with(line->buf, "diff -"))
		return 1;

	/* CVS "Index: " line? */
	if (starts_with(line->buf, "Index: "))
		return 1;

	/*
	 * "--- <filename>" starts patches without headers
	 * "---<sp>*" is a manual separator
	 */
	if (line->len < 4)
		return 0;

	if (starts_with(line->buf, "---")) {
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

static int is_scissors_line(const char *line)
{
	const char *c;
	int scissors = 0, gap = 0;
	const char *first_nonblank = NULL, *last_nonblank = NULL;
	int visible, perforation = 0, in_perforation = 0;

	for (c = line; *c; c++) {
		if (isspace(*c)) {
			if (in_perforation) {
				perforation++;
				gap++;
			}
			continue;
		}
		last_nonblank = c;
		if (!first_nonblank)
			first_nonblank = c;
		if (*c == '-') {
			in_perforation = 1;
			perforation++;
			continue;
		}
		if (starts_with(c, ">8") || starts_with(c, "8<") ||
		    starts_with(c, ">%") || starts_with(c, "%<")) {
			in_perforation = 1;
			perforation += 2;
			scissors += 2;
			c++;
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

	if (first_nonblank && last_nonblank)
		visible = last_nonblank - first_nonblank + 1;
	else
		visible = 0;
	return (scissors && 8 <= visible &&
		visible < perforation * 3 &&
		gap * 2 < perforation);
}

static void flush_inbody_header_accum(struct mailinfo *mi)
{
	if (!mi->inbody_header_accum.len)
		return;
	if (!check_header(mi, &mi->inbody_header_accum, mi->s_hdr_data, 0))
		BUG("inbody_header_accum, if not empty, must always contain a valid in-body header");
	strbuf_reset(&mi->inbody_header_accum);
}

static int check_inbody_header(struct mailinfo *mi, const struct strbuf *line)
{
	if (mi->inbody_header_accum.len &&
	    (line->buf[0] == ' ' || line->buf[0] == '\t')) {
		if (mi->use_scissors && is_scissors_line(line->buf)) {
			/*
			 * This is a scissors line; do not consider this line
			 * as a header continuation line.
			 */
			flush_inbody_header_accum(mi);
			return 0;
		}
		strbuf_strip_suffix(&mi->inbody_header_accum, "\n");
		strbuf_addbuf(&mi->inbody_header_accum, line);
		return 1;
	}

	flush_inbody_header_accum(mi);

	if (starts_with(line->buf, ">From") && isspace(line->buf[5]))
		return is_format_patch_separator(line->buf + 1, line->len - 1);
	if (starts_with(line->buf, "[PATCH]") && isspace(line->buf[7])) {
		int i;
		for (i = 0; header[i]; i++)
			if (!strcmp("Subject", header[i])) {
				handle_header(&mi->s_hdr_data[i], line);
				return 1;
			}
		return 0;
	}
	if (is_inbody_header(mi, line)) {
		strbuf_addbuf(&mi->inbody_header_accum, line);
		return 1;
	}
	return 0;
}

static int handle_commit_msg(struct mailinfo *mi, struct strbuf *line)
{
	assert(!mi->filter_stage);

	if (mi->header_stage) {
		if (!line->len || (line->len == 1 && line->buf[0] == '\n')) {
			if (mi->inbody_header_accum.len) {
				flush_inbody_header_accum(mi);
				mi->header_stage = 0;
			}
			return 0;
		}
	}

	if (mi->use_inbody_headers && mi->header_stage) {
		mi->header_stage = check_inbody_header(mi, line);
		if (mi->header_stage)
			return 0;
	} else
		/* Only trim the first (blank) line of the commit message
		 * when ignoring in-body headers.
		 */
		mi->header_stage = 0;

	/* normalize the log message to UTF-8. */
	if (convert_to_utf8(mi, line, mi->charset.buf))
		return 0; /* mi->input_error already set */

	if (mi->use_scissors && is_scissors_line(line->buf)) {
		int i;

		strbuf_setlen(&mi->log_message, 0);
		mi->header_stage = 1;

		/*
		 * We may have already read "secondary headers"; purge
		 * them to give ourselves a clean restart.
		 */
		for (i = 0; header[i]; i++) {
			if (mi->s_hdr_data[i])
				strbuf_release(mi->s_hdr_data[i]);
			FREE_AND_NULL(mi->s_hdr_data[i]);
		}
		return 0;
	}

	if (patchbreak(line)) {
		if (mi->message_id)
			strbuf_addf(&mi->log_message,
				    "Message-ID: %s\n", mi->message_id);
		return 1;
	}

	strbuf_addbuf(&mi->log_message, line);
	return 0;
}

static void handle_patch(struct mailinfo *mi, const struct strbuf *line)
{
	fwrite(line->buf, 1, line->len, mi->patchfile);
	mi->patch_lines++;
}

static void handle_filter(struct mailinfo *mi, struct strbuf *line)
{
	switch (mi->filter_stage) {
	case 0:
		if (!handle_commit_msg(mi, line))
			break;
		mi->filter_stage++;
		/* fallthrough */
	case 1:
		handle_patch(mi, line);
		break;
	}
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
	if (starts_with(cp, "From ") || starts_with(cp, ">From "))
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
	struct strbuf continuation = STRBUF_INIT;

	/* Get the first part of the line. */
	if (strbuf_getline_lf(line, in))
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

		peek = fgetc(in);
		if (peek == EOF)
			break;
		ungetc(peek, in);
		if (peek != ' ' && peek != '\t')
			break;
		if (strbuf_getline_lf(&continuation, in))
			break;
		continuation.buf[0] = ' ';
		strbuf_rtrim(&continuation);
		strbuf_addbuf(line, &continuation);
	}
	strbuf_release(&continuation);

	return 1;
}

static int find_boundary(struct mailinfo *mi, struct strbuf *line)
{
	while (!strbuf_getline_lf(line, mi->input)) {
		if (*(mi->content_top) && is_multipart_boundary(mi, line))
			return 1;
	}
	return 0;
}

static int handle_boundary(struct mailinfo *mi, struct strbuf *line)
{
	struct strbuf newline = STRBUF_INIT;

	strbuf_addch(&newline, '\n');
again:
	if (line->len >= (*(mi->content_top))->len + 2 &&
	    !memcmp(line->buf + (*(mi->content_top))->len, "--", 2)) {
		/* we hit an end boundary */
		/* pop the current boundary off the stack */
		strbuf_release(*(mi->content_top));
		FREE_AND_NULL(*(mi->content_top));

		/* technically won't happen as is_multipart_boundary()
		   will fail first.  But just in case..
		 */
		if (--mi->content_top < mi->content) {
			error("Detected mismatched boundaries, can't recover");
			mi->input_error = -1;
			mi->content_top = mi->content;
			strbuf_release(&newline);
			return 0;
		}
		handle_filter(mi, &newline);
		strbuf_release(&newline);
		if (mi->input_error)
			return 0;

		/* skip to the next boundary */
		if (!find_boundary(mi, line))
			return 0;
		goto again;
	}

	/* set some defaults */
	mi->transfer_encoding = TE_DONTCARE;
	strbuf_reset(&mi->charset);

	/* slurp in this section's info */
	while (read_one_header_line(line, mi->input))
		check_header(mi, line, mi->p_hdr_data, 0);

	strbuf_release(&newline);
	/* replenish line */
	if (strbuf_getline_lf(line, mi->input))
		return 0;
	strbuf_addch(line, '\n');
	return 1;
}

static void handle_filter_flowed(struct mailinfo *mi, struct strbuf *line,
				 struct strbuf *prev)
{
	size_t len = line->len;
	const char *rest;

	if (!mi->format_flowed) {
		if (len >= 2 &&
		    line->buf[len - 2] == '\r' &&
		    line->buf[len - 1] == '\n') {
			mi->have_quoted_cr = 1;
			if (mi->quoted_cr == quoted_cr_strip) {
				strbuf_setlen(line, len - 2);
				strbuf_addch(line, '\n');
				len--;
			}
		}
		handle_filter(mi, line);
		return;
	}

	if (line->buf[len - 1] == '\n') {
		len--;
		if (len && line->buf[len - 1] == '\r')
			len--;
	}

	/* Keep signature separator as-is. */
	if (skip_prefix(line->buf, "-- ", &rest) && rest - line->buf == len) {
		if (prev->len) {
			handle_filter(mi, prev);
			strbuf_reset(prev);
		}
		handle_filter(mi, line);
		return;
	}

	/* Unstuff space-stuffed line. */
	if (len && line->buf[0] == ' ') {
		strbuf_remove(line, 0, 1);
		len--;
	}

	/* Save flowed line for later, but without the soft line break. */
	if (len && line->buf[len - 1] == ' ') {
		strbuf_add(prev, line->buf, len - !!mi->delsp);
		return;
	}

	/* Prepend any previous partial lines */
	strbuf_insert(line, 0, prev->buf, prev->len);
	strbuf_reset(prev);

	handle_filter(mi, line);
}

static void summarize_quoted_cr(struct mailinfo *mi)
{
	if (mi->have_quoted_cr &&
	    mi->quoted_cr == quoted_cr_warn)
		warning(_("quoted CRLF detected"));
}

static void handle_body(struct mailinfo *mi, struct strbuf *line)
{
	struct strbuf prev = STRBUF_INIT;

	/* Skip up to the first boundary */
	if (*(mi->content_top)) {
		if (!find_boundary(mi, line))
			goto handle_body_out;
	}

	do {
		/* process any boundary lines */
		if (*(mi->content_top) && is_multipart_boundary(mi, line)) {
			/* flush any leftover */
			if (prev.len) {
				handle_filter(mi, &prev);
				strbuf_reset(&prev);
			}
			summarize_quoted_cr(mi);
			mi->have_quoted_cr = 0;
			if (!handle_boundary(mi, line))
				goto handle_body_out;
		}

		/* Unwrap transfer encoding */
		decode_transfer_encoding(mi, line);

		switch (mi->transfer_encoding) {
		case TE_BASE64:
		case TE_QP:
		{
			struct strbuf **lines, **it, *sb;

			/* Prepend any previous partial lines */
			strbuf_insert(line, 0, prev.buf, prev.len);
			strbuf_reset(&prev);

			/*
			 * This is a decoded line that may contain
			 * multiple new lines.  Pass only one chunk
			 * at a time to handle_filter()
			 */
			lines = strbuf_split(line, '\n');
			for (it = lines; (sb = *it); it++) {
				if (!*(it + 1)) /* The last line */
					if (sb->buf[sb->len - 1] != '\n') {
						/* Partial line, save it for later. */
						strbuf_addbuf(&prev, sb);
						break;
					}
				handle_filter_flowed(mi, sb, &prev);
			}
			/*
			 * The partial chunk is saved in "prev" and will be
			 * appended by the next iteration of read_line_with_nul().
			 */
			strbuf_list_free(lines);
			break;
		}
		default:
			handle_filter_flowed(mi, line, &prev);
		}

		if (mi->input_error)
			break;
	} while (!strbuf_getwholeline(line, mi->input, '\n'));

	if (prev.len)
		handle_filter(mi, &prev);
	summarize_quoted_cr(mi);

	flush_inbody_header_accum(mi);

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

static void handle_info(struct mailinfo *mi)
{
	struct strbuf *hdr;
	int i;

	for (i = 0; header[i]; i++) {
		/* only print inbody headers if we output a patch file */
		if (mi->patch_lines && mi->s_hdr_data[i])
			hdr = mi->s_hdr_data[i];
		else if (mi->p_hdr_data[i])
			hdr = mi->p_hdr_data[i];
		else
			continue;

		if (memchr(hdr->buf, '\0', hdr->len)) {
			error("a NUL byte in '%s' is not allowed.", header[i]);
			mi->input_error = -1;
		}

		if (!strcmp(header[i], "Subject")) {
			if (!mi->keep_subject) {
				cleanup_subject(mi, hdr);
				cleanup_space(hdr);
			}
			output_header_lines(mi->output, "Subject", hdr);
		} else if (!strcmp(header[i], "From")) {
			cleanup_space(hdr);
			handle_from(mi, hdr);
			fprintf(mi->output, "Author: %s\n", mi->name.buf);
			fprintf(mi->output, "Email: %s\n", mi->email.buf);
		} else {
			cleanup_space(hdr);
			fprintf(mi->output, "%s: %s\n", header[i], hdr->buf);
		}
	}
	fprintf(mi->output, "\n");
}

int mailinfo(struct mailinfo *mi, const char *msg, const char *patch)
{
	FILE *cmitmsg;
	int peek;
	struct strbuf line = STRBUF_INIT;

	cmitmsg = fopen(msg, "w");
	if (!cmitmsg) {
		perror(msg);
		return -1;
	}
	mi->patchfile = fopen(patch, "w");
	if (!mi->patchfile) {
		perror(patch);
		fclose(cmitmsg);
		return -1;
	}

	mi->p_hdr_data = xcalloc(MAX_HDR_PARSED, sizeof(*(mi->p_hdr_data)));
	mi->s_hdr_data = xcalloc(MAX_HDR_PARSED, sizeof(*(mi->s_hdr_data)));

	do {
		peek = fgetc(mi->input);
		if (peek == EOF) {
			fclose(cmitmsg);
			return error("empty patch: '%s'", patch);
		}
	} while (isspace(peek));
	ungetc(peek, mi->input);

	/* process the email header */
	while (read_one_header_line(&line, mi->input))
		check_header(mi, &line, mi->p_hdr_data, 1);

	handle_body(mi, &line);
	fwrite(mi->log_message.buf, 1, mi->log_message.len, cmitmsg);
	fclose(cmitmsg);
	fclose(mi->patchfile);

	handle_info(mi);
	strbuf_release(&line);
	return mi->input_error;
}

int mailinfo_parse_quoted_cr_action(const char *actionstr, int *action)
{
	if (!strcmp(actionstr, "nowarn"))
		*action = quoted_cr_nowarn;
	else if (!strcmp(actionstr, "warn"))
		*action = quoted_cr_warn;
	else if (!strcmp(actionstr, "strip"))
		*action = quoted_cr_strip;
	else
		return -1;
	return 0;
}

static int git_mailinfo_config(const char *var, const char *value, void *mi_)
{
	struct mailinfo *mi = mi_;

	if (!starts_with(var, "mailinfo."))
		return git_default_config(var, value, NULL);
	if (!strcmp(var, "mailinfo.scissors")) {
		mi->use_scissors = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "mailinfo.quotedcr")) {
		if (mailinfo_parse_quoted_cr_action(value, &mi->quoted_cr) != 0)
			return error(_("bad action '%s' for '%s'"), value, var);
		return 0;
	}
	/* perhaps others here */
	return 0;
}

void setup_mailinfo(struct mailinfo *mi)
{
	memset(mi, 0, sizeof(*mi));
	strbuf_init(&mi->name, 0);
	strbuf_init(&mi->email, 0);
	strbuf_init(&mi->charset, 0);
	strbuf_init(&mi->log_message, 0);
	strbuf_init(&mi->inbody_header_accum, 0);
	mi->quoted_cr = quoted_cr_warn;
	mi->header_stage = 1;
	mi->use_inbody_headers = 1;
	mi->content_top = mi->content;
	git_config(git_mailinfo_config, mi);
}

void clear_mailinfo(struct mailinfo *mi)
{
	strbuf_release(&mi->name);
	strbuf_release(&mi->email);
	strbuf_release(&mi->charset);
	strbuf_release(&mi->inbody_header_accum);
	free(mi->message_id);

	strbuf_list_free(mi->p_hdr_data);
	strbuf_list_free(mi->s_hdr_data);

	while (mi->content < mi->content_top) {
		free(*(mi->content_top));
		mi->content_top--;
	}

	strbuf_release(&mi->log_message);
}
