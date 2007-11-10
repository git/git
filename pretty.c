#include "cache.h"
#include "commit.h"
#include "interpolate.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"

static struct cmt_fmt_map {
	const char *n;
	size_t cmp_len;
	enum cmit_fmt v;
} cmt_fmts[] = {
	{ "raw",	1,	CMIT_FMT_RAW },
	{ "medium",	1,	CMIT_FMT_MEDIUM },
	{ "short",	1,	CMIT_FMT_SHORT },
	{ "email",	1,	CMIT_FMT_EMAIL },
	{ "full",	5,	CMIT_FMT_FULL },
	{ "fuller",	5,	CMIT_FMT_FULLER },
	{ "oneline",	1,	CMIT_FMT_ONELINE },
	{ "format:",	7,	CMIT_FMT_USERFORMAT},
};

static char *user_format;

enum cmit_fmt get_commit_format(const char *arg)
{
	int i;

	if (!arg || !*arg)
		return CMIT_FMT_DEFAULT;
	if (*arg == '=')
		arg++;
	if (!prefixcmp(arg, "format:")) {
		if (user_format)
			free(user_format);
		user_format = xstrdup(arg + 7);
		return CMIT_FMT_USERFORMAT;
	}
	for (i = 0; i < ARRAY_SIZE(cmt_fmts); i++) {
		if (!strncmp(arg, cmt_fmts[i].n, cmt_fmts[i].cmp_len) &&
		    !strncmp(arg, cmt_fmts[i].n, strlen(arg)))
			return cmt_fmts[i].v;
	}

	die("invalid --pretty format: %s", arg);
}

/*
 * Generic support for pretty-printing the header
 */
static int get_one_line(const char *msg)
{
	int ret = 0;

	for (;;) {
		char c = *msg++;
		if (!c)
			break;
		ret++;
		if (c == '\n')
			break;
	}
	return ret;
}

/* High bit set, or ISO-2022-INT */
int non_ascii(int ch)
{
	ch = (ch & 0xff);
	return ((ch & 0x80) || (ch == 0x1b));
}

static int is_rfc2047_special(char ch)
{
	return (non_ascii(ch) || (ch == '=') || (ch == '?') || (ch == '_'));
}

static void add_rfc2047(struct strbuf *sb, const char *line, int len,
		       const char *encoding)
{
	int i, last;

	for (i = 0; i < len; i++) {
		int ch = line[i];
		if (non_ascii(ch))
			goto needquote;
		if ((i + 1 < len) && (ch == '=' && line[i+1] == '?'))
			goto needquote;
	}
	strbuf_add(sb, line, len);
	return;

needquote:
	strbuf_grow(sb, len * 3 + strlen(encoding) + 100);
	strbuf_addf(sb, "=?%s?q?", encoding);
	for (i = last = 0; i < len; i++) {
		unsigned ch = line[i] & 0xFF;
		/*
		 * We encode ' ' using '=20' even though rfc2047
		 * allows using '_' for readability.  Unfortunately,
		 * many programs do not understand this and just
		 * leave the underscore in place.
		 */
		if (is_rfc2047_special(ch) || ch == ' ') {
			strbuf_add(sb, line + last, i - last);
			strbuf_addf(sb, "=%02X", ch);
			last = i + 1;
		}
	}
	strbuf_add(sb, line + last, len - last);
	strbuf_addstr(sb, "?=");
}

static void add_user_info(const char *what, enum cmit_fmt fmt, struct strbuf *sb,
			 const char *line, enum date_mode dmode,
			 const char *encoding)
{
	char *date;
	int namelen;
	unsigned long time;
	int tz;
	const char *filler = "    ";

	if (fmt == CMIT_FMT_ONELINE)
		return;
	date = strchr(line, '>');
	if (!date)
		return;
	namelen = ++date - line;
	time = strtoul(date, &date, 10);
	tz = strtol(date, NULL, 10);

	if (fmt == CMIT_FMT_EMAIL) {
		char *name_tail = strchr(line, '<');
		int display_name_length;
		if (!name_tail)
			return;
		while (line < name_tail && isspace(name_tail[-1]))
			name_tail--;
		display_name_length = name_tail - line;
		filler = "";
		strbuf_addstr(sb, "From: ");
		add_rfc2047(sb, line, display_name_length, encoding);
		strbuf_add(sb, name_tail, namelen - display_name_length);
		strbuf_addch(sb, '\n');
	} else {
		strbuf_addf(sb, "%s: %.*s%.*s\n", what,
			      (fmt == CMIT_FMT_FULLER) ? 4 : 0,
			      filler, namelen, line);
	}
	switch (fmt) {
	case CMIT_FMT_MEDIUM:
		strbuf_addf(sb, "Date:   %s\n", show_date(time, tz, dmode));
		break;
	case CMIT_FMT_EMAIL:
		strbuf_addf(sb, "Date: %s\n", show_date(time, tz, DATE_RFC2822));
		break;
	case CMIT_FMT_FULLER:
		strbuf_addf(sb, "%sDate: %s\n", what, show_date(time, tz, dmode));
		break;
	default:
		/* notin' */
		break;
	}
}

static int is_empty_line(const char *line, int *len_p)
{
	int len = *len_p;
	while (len && isspace(line[len-1]))
		len--;
	*len_p = len;
	return !len;
}

static void add_merge_info(enum cmit_fmt fmt, struct strbuf *sb,
			const struct commit *commit, int abbrev)
{
	struct commit_list *parent = commit->parents;

	if ((fmt == CMIT_FMT_ONELINE) || (fmt == CMIT_FMT_EMAIL) ||
	    !parent || !parent->next)
		return;

	strbuf_addstr(sb, "Merge:");

	while (parent) {
		struct commit *p = parent->item;
		const char *hex = NULL;
		const char *dots;
		if (abbrev)
			hex = find_unique_abbrev(p->object.sha1, abbrev);
		if (!hex)
			hex = sha1_to_hex(p->object.sha1);
		dots = (abbrev && strlen(hex) != 40) ?  "..." : "";
		parent = parent->next;

		strbuf_addf(sb, " %s%s", hex, dots);
	}
	strbuf_addch(sb, '\n');
}

static char *get_header(const struct commit *commit, const char *key)
{
	int key_len = strlen(key);
	const char *line = commit->buffer;

	for (;;) {
		const char *eol = strchr(line, '\n'), *next;

		if (line == eol)
			return NULL;
		if (!eol) {
			eol = line + strlen(line);
			next = NULL;
		} else
			next = eol + 1;
		if (eol - line > key_len &&
		    !strncmp(line, key, key_len) &&
		    line[key_len] == ' ') {
			return xmemdupz(line + key_len + 1, eol - line - key_len - 1);
		}
		line = next;
	}
}

static char *replace_encoding_header(char *buf, const char *encoding)
{
	struct strbuf tmp;
	size_t start, len;
	char *cp = buf;

	/* guess if there is an encoding header before a \n\n */
	while (strncmp(cp, "encoding ", strlen("encoding "))) {
		cp = strchr(cp, '\n');
		if (!cp || *++cp == '\n')
			return buf;
	}
	start = cp - buf;
	cp = strchr(cp, '\n');
	if (!cp)
		return buf; /* should not happen but be defensive */
	len = cp + 1 - (buf + start);

	strbuf_init(&tmp, 0);
	strbuf_attach(&tmp, buf, strlen(buf), strlen(buf) + 1);
	if (is_encoding_utf8(encoding)) {
		/* we have re-coded to UTF-8; drop the header */
		strbuf_remove(&tmp, start, len);
	} else {
		/* just replaces XXXX in 'encoding XXXX\n' */
		strbuf_splice(&tmp, start + strlen("encoding "),
					  len - strlen("encoding \n"),
					  encoding, strlen(encoding));
	}
	return strbuf_detach(&tmp, NULL);
}

static char *logmsg_reencode(const struct commit *commit,
			     const char *output_encoding)
{
	static const char *utf8 = "utf-8";
	const char *use_encoding;
	char *encoding;
	char *out;

	if (!*output_encoding)
		return NULL;
	encoding = get_header(commit, "encoding");
	use_encoding = encoding ? encoding : utf8;
	if (!strcmp(use_encoding, output_encoding))
		if (encoding) /* we'll strip encoding header later */
			out = xstrdup(commit->buffer);
		else
			return NULL; /* nothing to do */
	else
		out = reencode_string(commit->buffer,
				      output_encoding, use_encoding);
	if (out)
		out = replace_encoding_header(out, output_encoding);

	free(encoding);
	return out;
}

static void fill_person(struct interp *table, const char *msg, int len)
{
	int start, end, tz = 0;
	unsigned long date;
	char *ep;

	/* parse name */
	for (end = 0; end < len && msg[end] != '<'; end++)
		; /* do nothing */
	start = end + 1;
	while (end > 0 && isspace(msg[end - 1]))
		end--;
	table[0].value = xmemdupz(msg, end);

	if (start >= len)
		return;

	/* parse email */
	for (end = start + 1; end < len && msg[end] != '>'; end++)
		; /* do nothing */

	if (end >= len)
		return;

	table[1].value = xmemdupz(msg + start, end - start);

	/* parse date */
	for (start = end + 1; start < len && isspace(msg[start]); start++)
		; /* do nothing */
	if (start >= len)
		return;
	date = strtoul(msg + start, &ep, 10);
	if (msg + start == ep)
		return;

	table[5].value = xmemdupz(msg + start, ep - (msg + start));

	/* parse tz */
	for (start = ep - msg + 1; start < len && isspace(msg[start]); start++)
		; /* do nothing */
	if (start + 1 < len) {
		tz = strtoul(msg + start + 1, NULL, 10);
		if (msg[start] == '-')
			tz = -tz;
	}

	interp_set_entry(table, 2, show_date(date, tz, DATE_NORMAL));
	interp_set_entry(table, 3, show_date(date, tz, DATE_RFC2822));
	interp_set_entry(table, 4, show_date(date, tz, DATE_RELATIVE));
	interp_set_entry(table, 6, show_date(date, tz, DATE_ISO8601));
}

void format_commit_message(const struct commit *commit,
                           const void *format, struct strbuf *sb)
{
	struct interp table[] = {
		{ "%H" },	/* commit hash */
		{ "%h" },	/* abbreviated commit hash */
		{ "%T" },	/* tree hash */
		{ "%t" },	/* abbreviated tree hash */
		{ "%P" },	/* parent hashes */
		{ "%p" },	/* abbreviated parent hashes */
		{ "%an" },	/* author name */
		{ "%ae" },	/* author email */
		{ "%ad" },	/* author date */
		{ "%aD" },	/* author date, RFC2822 style */
		{ "%ar" },	/* author date, relative */
		{ "%at" },	/* author date, UNIX timestamp */
		{ "%ai" },	/* author date, ISO 8601 */
		{ "%cn" },	/* committer name */
		{ "%ce" },	/* committer email */
		{ "%cd" },	/* committer date */
		{ "%cD" },	/* committer date, RFC2822 style */
		{ "%cr" },	/* committer date, relative */
		{ "%ct" },	/* committer date, UNIX timestamp */
		{ "%ci" },	/* committer date, ISO 8601 */
		{ "%e" },	/* encoding */
		{ "%s" },	/* subject */
		{ "%b" },	/* body */
		{ "%Cred" },	/* red */
		{ "%Cgreen" },	/* green */
		{ "%Cblue" },	/* blue */
		{ "%Creset" },	/* reset color */
		{ "%n" },	/* newline */
		{ "%m" },	/* left/right/bottom */
	};
	enum interp_index {
		IHASH = 0, IHASH_ABBREV,
		ITREE, ITREE_ABBREV,
		IPARENTS, IPARENTS_ABBREV,
		IAUTHOR_NAME, IAUTHOR_EMAIL,
		IAUTHOR_DATE, IAUTHOR_DATE_RFC2822, IAUTHOR_DATE_RELATIVE,
		IAUTHOR_TIMESTAMP, IAUTHOR_ISO8601,
		ICOMMITTER_NAME, ICOMMITTER_EMAIL,
		ICOMMITTER_DATE, ICOMMITTER_DATE_RFC2822,
		ICOMMITTER_DATE_RELATIVE, ICOMMITTER_TIMESTAMP,
		ICOMMITTER_ISO8601,
		IENCODING,
		ISUBJECT,
		IBODY,
		IRED, IGREEN, IBLUE, IRESET_COLOR,
		INEWLINE,
		ILEFT_RIGHT,
	};
	struct commit_list *p;
	char parents[1024];
	unsigned long len;
	int i;
	enum { HEADER, SUBJECT, BODY } state;
	const char *msg = commit->buffer;

	if (ILEFT_RIGHT + 1 != ARRAY_SIZE(table))
		die("invalid interp table!");

	/* these are independent of the commit */
	interp_set_entry(table, IRED, "\033[31m");
	interp_set_entry(table, IGREEN, "\033[32m");
	interp_set_entry(table, IBLUE, "\033[34m");
	interp_set_entry(table, IRESET_COLOR, "\033[m");
	interp_set_entry(table, INEWLINE, "\n");

	/* these depend on the commit */
	if (!commit->object.parsed)
		parse_object(commit->object.sha1);
	interp_set_entry(table, IHASH, sha1_to_hex(commit->object.sha1));
	interp_set_entry(table, IHASH_ABBREV,
			find_unique_abbrev(commit->object.sha1,
				DEFAULT_ABBREV));
	interp_set_entry(table, ITREE, sha1_to_hex(commit->tree->object.sha1));
	interp_set_entry(table, ITREE_ABBREV,
			find_unique_abbrev(commit->tree->object.sha1,
				DEFAULT_ABBREV));
	interp_set_entry(table, ILEFT_RIGHT,
			 (commit->object.flags & BOUNDARY)
			 ? "-"
			 : (commit->object.flags & SYMMETRIC_LEFT)
			 ? "<"
			 : ">");

	parents[1] = 0;
	for (i = 0, p = commit->parents;
			p && i < sizeof(parents) - 1;
			p = p->next)
		i += snprintf(parents + i, sizeof(parents) - i - 1, " %s",
			sha1_to_hex(p->item->object.sha1));
	interp_set_entry(table, IPARENTS, parents + 1);

	parents[1] = 0;
	for (i = 0, p = commit->parents;
			p && i < sizeof(parents) - 1;
			p = p->next)
		i += snprintf(parents + i, sizeof(parents) - i - 1, " %s",
			find_unique_abbrev(p->item->object.sha1,
				DEFAULT_ABBREV));
	interp_set_entry(table, IPARENTS_ABBREV, parents + 1);

	for (i = 0, state = HEADER; msg[i] && state < BODY; i++) {
		int eol;
		for (eol = i; msg[eol] && msg[eol] != '\n'; eol++)
			; /* do nothing */

		if (state == SUBJECT) {
			table[ISUBJECT].value = xmemdupz(msg + i, eol - i);
			i = eol;
		}
		if (i == eol) {
			state++;
			/* strip empty lines */
			while (msg[eol + 1] == '\n')
				eol++;
		} else if (!prefixcmp(msg + i, "author "))
			fill_person(table + IAUTHOR_NAME,
					msg + i + 7, eol - i - 7);
		else if (!prefixcmp(msg + i, "committer "))
			fill_person(table + ICOMMITTER_NAME,
					msg + i + 10, eol - i - 10);
		else if (!prefixcmp(msg + i, "encoding "))
			table[IENCODING].value =
				xmemdupz(msg + i + 9, eol - i - 9);
		i = eol;
	}
	if (msg[i])
		table[IBODY].value = xstrdup(msg + i);

	len = interpolate(sb->buf + sb->len, strbuf_avail(sb),
				format, table, ARRAY_SIZE(table));
	if (len > strbuf_avail(sb)) {
		strbuf_grow(sb, len);
		interpolate(sb->buf + sb->len, strbuf_avail(sb) + 1,
					format, table, ARRAY_SIZE(table));
	}
	strbuf_setlen(sb, sb->len + len);
	interp_clear_table(table, ARRAY_SIZE(table));
}

static void pp_header(enum cmit_fmt fmt,
		      int abbrev,
		      enum date_mode dmode,
		      const char *encoding,
		      const struct commit *commit,
		      const char **msg_p,
		      struct strbuf *sb)
{
	int parents_shown = 0;

	for (;;) {
		const char *line = *msg_p;
		int linelen = get_one_line(*msg_p);

		if (!linelen)
			return;
		*msg_p += linelen;

		if (linelen == 1)
			/* End of header */
			return;

		if (fmt == CMIT_FMT_RAW) {
			strbuf_add(sb, line, linelen);
			continue;
		}

		if (!memcmp(line, "parent ", 7)) {
			if (linelen != 48)
				die("bad parent line in commit");
			continue;
		}

		if (!parents_shown) {
			struct commit_list *parent;
			int num;
			for (parent = commit->parents, num = 0;
			     parent;
			     parent = parent->next, num++)
				;
			/* with enough slop */
			strbuf_grow(sb, num * 50 + 20);
			add_merge_info(fmt, sb, commit, abbrev);
			parents_shown = 1;
		}

		/*
		 * MEDIUM == DEFAULT shows only author with dates.
		 * FULL shows both authors but not dates.
		 * FULLER shows both authors and dates.
		 */
		if (!memcmp(line, "author ", 7)) {
			strbuf_grow(sb, linelen + 80);
			add_user_info("Author", fmt, sb, line + 7, dmode, encoding);
		}
		if (!memcmp(line, "committer ", 10) &&
		    (fmt == CMIT_FMT_FULL || fmt == CMIT_FMT_FULLER)) {
			strbuf_grow(sb, linelen + 80);
			add_user_info("Commit", fmt, sb, line + 10, dmode, encoding);
		}
	}
}

static void pp_title_line(enum cmit_fmt fmt,
			  const char **msg_p,
			  struct strbuf *sb,
			  const char *subject,
			  const char *after_subject,
			  const char *encoding,
			  int plain_non_ascii)
{
	struct strbuf title;

	strbuf_init(&title, 80);

	for (;;) {
		const char *line = *msg_p;
		int linelen = get_one_line(line);

		*msg_p += linelen;
		if (!linelen || is_empty_line(line, &linelen))
			break;

		strbuf_grow(&title, linelen + 2);
		if (title.len) {
			if (fmt == CMIT_FMT_EMAIL) {
				strbuf_addch(&title, '\n');
			}
			strbuf_addch(&title, ' ');
		}
		strbuf_add(&title, line, linelen);
	}

	strbuf_grow(sb, title.len + 1024);
	if (subject) {
		strbuf_addstr(sb, subject);
		add_rfc2047(sb, title.buf, title.len, encoding);
	} else {
		strbuf_addbuf(sb, &title);
	}
	strbuf_addch(sb, '\n');

	if (plain_non_ascii) {
		const char *header_fmt =
			"MIME-Version: 1.0\n"
			"Content-Type: text/plain; charset=%s\n"
			"Content-Transfer-Encoding: 8bit\n";
		strbuf_addf(sb, header_fmt, encoding);
	}
	if (after_subject) {
		strbuf_addstr(sb, after_subject);
	}
	if (fmt == CMIT_FMT_EMAIL) {
		strbuf_addch(sb, '\n');
	}
	strbuf_release(&title);
}

static void pp_remainder(enum cmit_fmt fmt,
			 const char **msg_p,
			 struct strbuf *sb,
			 int indent)
{
	int first = 1;
	for (;;) {
		const char *line = *msg_p;
		int linelen = get_one_line(line);
		*msg_p += linelen;

		if (!linelen)
			break;

		if (is_empty_line(line, &linelen)) {
			if (first)
				continue;
			if (fmt == CMIT_FMT_SHORT)
				break;
		}
		first = 0;

		strbuf_grow(sb, linelen + indent + 20);
		if (indent) {
			memset(sb->buf + sb->len, ' ', indent);
			strbuf_setlen(sb, sb->len + indent);
		}
		strbuf_add(sb, line, linelen);
		strbuf_addch(sb, '\n');
	}
}

void pretty_print_commit(enum cmit_fmt fmt, const struct commit *commit,
				  struct strbuf *sb, int abbrev,
				  const char *subject, const char *after_subject,
				  enum date_mode dmode, int plain_non_ascii)
{
	unsigned long beginning_of_body;
	int indent = 4;
	const char *msg = commit->buffer;
	char *reencoded;
	const char *encoding;

	if (fmt == CMIT_FMT_USERFORMAT) {
		format_commit_message(commit, user_format, sb);
		return;
	}

	encoding = (git_log_output_encoding
		    ? git_log_output_encoding
		    : git_commit_encoding);
	if (!encoding)
		encoding = "utf-8";
	reencoded = logmsg_reencode(commit, encoding);
	if (reencoded) {
		msg = reencoded;
	}

	if (fmt == CMIT_FMT_ONELINE || fmt == CMIT_FMT_EMAIL)
		indent = 0;

	/* After-subject is used to pass in Content-Type: multipart
	 * MIME header; in that case we do not have to do the
	 * plaintext content type even if the commit message has
	 * non 7-bit ASCII character.  Otherwise, check if we need
	 * to say this is not a 7-bit ASCII.
	 */
	if (fmt == CMIT_FMT_EMAIL && !after_subject) {
		int i, ch, in_body;

		for (in_body = i = 0; (ch = msg[i]); i++) {
			if (!in_body) {
				/* author could be non 7-bit ASCII but
				 * the log may be so; skip over the
				 * header part first.
				 */
				if (ch == '\n' && msg[i+1] == '\n')
					in_body = 1;
			}
			else if (non_ascii(ch)) {
				plain_non_ascii = 1;
				break;
			}
		}
	}

	pp_header(fmt, abbrev, dmode, encoding, commit, &msg, sb);
	if (fmt != CMIT_FMT_ONELINE && !subject) {
		strbuf_addch(sb, '\n');
	}

	/* Skip excess blank lines at the beginning of body, if any... */
	for (;;) {
		int linelen = get_one_line(msg);
		int ll = linelen;
		if (!linelen)
			break;
		if (!is_empty_line(msg, &ll))
			break;
		msg += linelen;
	}

	/* These formats treat the title line specially. */
	if (fmt == CMIT_FMT_ONELINE || fmt == CMIT_FMT_EMAIL)
		pp_title_line(fmt, &msg, sb, subject,
			      after_subject, encoding, plain_non_ascii);

	beginning_of_body = sb->len;
	if (fmt != CMIT_FMT_ONELINE)
		pp_remainder(fmt, &msg, sb, indent);
	strbuf_rtrim(sb);

	/* Make sure there is an EOLN for the non-oneline case */
	if (fmt != CMIT_FMT_ONELINE)
		strbuf_addch(sb, '\n');

	/*
	 * The caller may append additional body text in e-mail
	 * format.  Make sure we did not strip the blank line
	 * between the header and the body.
	 */
	if (fmt == CMIT_FMT_EMAIL && sb->len <= beginning_of_body)
		strbuf_addch(sb, '\n');
	free(reencoded);
}
