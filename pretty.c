#include "cache.h"
#include "commit.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"
#include "string-list.h"
#include "mailmap.h"

static char *user_format;

void get_commit_format(const char *arg, struct rev_info *rev)
{
	int i;
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
	};

	rev->use_terminator = 0;
	if (!arg || !*arg) {
		rev->commit_format = CMIT_FMT_DEFAULT;
		return;
	}
	if (!prefixcmp(arg, "format:") || !prefixcmp(arg, "tformat:")) {
		const char *cp = strchr(arg, ':') + 1;
		free(user_format);
		user_format = xstrdup(cp);
		if (arg[0] == 't')
			rev->use_terminator = 1;
		rev->commit_format = CMIT_FMT_USERFORMAT;
		return;
	}
	for (i = 0; i < ARRAY_SIZE(cmt_fmts); i++) {
		if (!strncmp(arg, cmt_fmts[i].n, cmt_fmts[i].cmp_len) &&
		    !strncmp(arg, cmt_fmts[i].n, strlen(arg))) {
			if (cmt_fmts[i].v == CMIT_FMT_ONELINE)
				rev->use_terminator = 1;
			rev->commit_format = cmt_fmts[i].v;
			return;
		}
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

void pp_user_info(const char *what, enum cmit_fmt fmt, struct strbuf *sb,
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

static int mailmap_name(struct strbuf *sb, const char *email)
{
	static struct string_list *mail_map;
	char buffer[1024];

	if (!mail_map) {
		mail_map = xcalloc(1, sizeof(*mail_map));
		read_mailmap(mail_map, ".mailmap", NULL);
	}

	if (!mail_map->nr)
		return -1;

	if (!map_email(mail_map, email, buffer, sizeof(buffer)))
		return -1;
	strbuf_addstr(sb, buffer);
	return 0;
}

static size_t format_person_part(struct strbuf *sb, char part,
                               const char *msg, int len)
{
	/* currently all placeholders have same length */
	const int placeholder_len = 2;
	int start, end, tz = 0;
	unsigned long date = 0;
	char *ep;

	/* advance 'end' to point to email start delimiter */
	for (end = 0; end < len && msg[end] != '<'; end++)
		; /* do nothing */

	/*
	 * When end points at the '<' that we found, it should have
	 * matching '>' later, which means 'end' must be strictly
	 * below len - 1.
	 */
	if (end >= len - 2)
		goto skip;

	if (part == 'n' || part == 'N') {	/* name */
		while (end > 0 && isspace(msg[end - 1]))
			end--;
		if (part != 'N' || !msg[end] || !msg[end + 1] ||
		    mailmap_name(sb, msg + end + 2) < 0)
			strbuf_add(sb, msg, end);
		return placeholder_len;
	}
	start = ++end; /* save email start position */

	/* advance 'end' to point to email end delimiter */
	for ( ; end < len && msg[end] != '>'; end++)
		; /* do nothing */

	if (end >= len)
		goto skip;

	if (part == 'e') {	/* email */
		strbuf_add(sb, msg + start, end - start);
		return placeholder_len;
	}

	/* advance 'start' to point to date start delimiter */
	for (start = end + 1; start < len && isspace(msg[start]); start++)
		; /* do nothing */
	if (start >= len)
		goto skip;
	date = strtoul(msg + start, &ep, 10);
	if (msg + start == ep)
		goto skip;

	if (part == 't') {	/* date, UNIX timestamp */
		strbuf_add(sb, msg + start, ep - (msg + start));
		return placeholder_len;
	}

	/* parse tz */
	for (start = ep - msg + 1; start < len && isspace(msg[start]); start++)
		; /* do nothing */
	if (start + 1 < len) {
		tz = strtoul(msg + start + 1, NULL, 10);
		if (msg[start] == '-')
			tz = -tz;
	}

	switch (part) {
	case 'd':	/* date */
		strbuf_addstr(sb, show_date(date, tz, DATE_NORMAL));
		return placeholder_len;
	case 'D':	/* date, RFC2822 style */
		strbuf_addstr(sb, show_date(date, tz, DATE_RFC2822));
		return placeholder_len;
	case 'r':	/* date, relative */
		strbuf_addstr(sb, show_date(date, tz, DATE_RELATIVE));
		return placeholder_len;
	case 'i':	/* date, ISO 8601 */
		strbuf_addstr(sb, show_date(date, tz, DATE_ISO8601));
		return placeholder_len;
	}

skip:
	/*
	 * bogus commit, 'sb' cannot be updated, but we still need to
	 * compute a valid return value.
	 */
	if (part == 'n' || part == 'e' || part == 't' || part == 'd'
	    || part == 'D' || part == 'r' || part == 'i')
		return placeholder_len;

	return 0; /* unknown placeholder */
}

struct chunk {
	size_t off;
	size_t len;
};

struct format_commit_context {
	const struct commit *commit;

	/* These offsets are relative to the start of the commit message. */
	int commit_header_parsed;
	struct chunk subject;
	struct chunk author;
	struct chunk committer;
	struct chunk encoding;
	size_t body_off;

	/* The following ones are relative to the result struct strbuf. */
	struct chunk abbrev_commit_hash;
	struct chunk abbrev_tree_hash;
	struct chunk abbrev_parent_hashes;
};

static int add_again(struct strbuf *sb, struct chunk *chunk)
{
	if (chunk->len) {
		strbuf_adddup(sb, chunk->off, chunk->len);
		return 1;
	}

	/*
	 * We haven't seen this chunk before.  Our caller is surely
	 * going to add it the hard way now.  Remember the most likely
	 * start of the to-be-added chunk: the current end of the
	 * struct strbuf.
	 */
	chunk->off = sb->len;
	return 0;
}

static void parse_commit_header(struct format_commit_context *context)
{
	const char *msg = context->commit->buffer;
	int i;
	enum { HEADER, SUBJECT, BODY } state;

	for (i = 0, state = HEADER; msg[i] && state < BODY; i++) {
		int eol;
		for (eol = i; msg[eol] && msg[eol] != '\n'; eol++)
			; /* do nothing */

		if (state == SUBJECT) {
			context->subject.off = i;
			context->subject.len = eol - i;
			i = eol;
		}
		if (i == eol) {
			state++;
			/* strip empty lines */
			while (msg[eol] == '\n' && msg[eol + 1] == '\n')
				eol++;
		} else if (!prefixcmp(msg + i, "author ")) {
			context->author.off = i + 7;
			context->author.len = eol - i - 7;
		} else if (!prefixcmp(msg + i, "committer ")) {
			context->committer.off = i + 10;
			context->committer.len = eol - i - 10;
		} else if (!prefixcmp(msg + i, "encoding ")) {
			context->encoding.off = i + 9;
			context->encoding.len = eol - i - 9;
		}
		i = eol;
		if (!msg[i])
			break;
	}
	context->body_off = i;
	context->commit_header_parsed = 1;
}

static size_t format_commit_item(struct strbuf *sb, const char *placeholder,
                               void *context)
{
	struct format_commit_context *c = context;
	const struct commit *commit = c->commit;
	const char *msg = commit->buffer;
	struct commit_list *p;
	int h1, h2;

	/* these are independent of the commit */
	switch (placeholder[0]) {
	case 'C':
		if (!prefixcmp(placeholder + 1, "red")) {
			strbuf_addstr(sb, "\033[31m");
			return 4;
		} else if (!prefixcmp(placeholder + 1, "green")) {
			strbuf_addstr(sb, "\033[32m");
			return 6;
		} else if (!prefixcmp(placeholder + 1, "blue")) {
			strbuf_addstr(sb, "\033[34m");
			return 5;
		} else if (!prefixcmp(placeholder + 1, "reset")) {
			strbuf_addstr(sb, "\033[m");
			return 6;
		} else
			return 0;
	case 'n':		/* newline */
		strbuf_addch(sb, '\n');
		return 1;
	case 'x':
		/* %x00 == NUL, %x0a == LF, etc. */
		if (0 <= (h1 = hexval_table[0xff & placeholder[1]]) &&
		    h1 <= 16 &&
		    0 <= (h2 = hexval_table[0xff & placeholder[2]]) &&
		    h2 <= 16) {
			strbuf_addch(sb, (h1<<4)|h2);
			return 3;
		} else
			return 0;
	}

	/* these depend on the commit */
	if (!commit->object.parsed)
		parse_object(commit->object.sha1);

	switch (placeholder[0]) {
	case 'H':		/* commit hash */
		strbuf_addstr(sb, sha1_to_hex(commit->object.sha1));
		return 1;
	case 'h':		/* abbreviated commit hash */
		if (add_again(sb, &c->abbrev_commit_hash))
			return 1;
		strbuf_addstr(sb, find_unique_abbrev(commit->object.sha1,
		                                     DEFAULT_ABBREV));
		c->abbrev_commit_hash.len = sb->len - c->abbrev_commit_hash.off;
		return 1;
	case 'T':		/* tree hash */
		strbuf_addstr(sb, sha1_to_hex(commit->tree->object.sha1));
		return 1;
	case 't':		/* abbreviated tree hash */
		if (add_again(sb, &c->abbrev_tree_hash))
			return 1;
		strbuf_addstr(sb, find_unique_abbrev(commit->tree->object.sha1,
		                                     DEFAULT_ABBREV));
		c->abbrev_tree_hash.len = sb->len - c->abbrev_tree_hash.off;
		return 1;
	case 'P':		/* parent hashes */
		for (p = commit->parents; p; p = p->next) {
			if (p != commit->parents)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, sha1_to_hex(p->item->object.sha1));
		}
		return 1;
	case 'p':		/* abbreviated parent hashes */
		if (add_again(sb, &c->abbrev_parent_hashes))
			return 1;
		for (p = commit->parents; p; p = p->next) {
			if (p != commit->parents)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, find_unique_abbrev(
					p->item->object.sha1, DEFAULT_ABBREV));
		}
		c->abbrev_parent_hashes.len = sb->len -
		                              c->abbrev_parent_hashes.off;
		return 1;
	case 'm':		/* left/right/bottom */
		strbuf_addch(sb, (commit->object.flags & BOUNDARY)
		                 ? '-'
		                 : (commit->object.flags & SYMMETRIC_LEFT)
		                 ? '<'
		                 : '>');
		return 1;
	}

	/* For the rest we have to parse the commit header. */
	if (!c->commit_header_parsed)
		parse_commit_header(c);

	switch (placeholder[0]) {
	case 's':	/* subject */
		strbuf_add(sb, msg + c->subject.off, c->subject.len);
		return 1;
	case 'a':	/* author ... */
		return format_person_part(sb, placeholder[1],
		                   msg + c->author.off, c->author.len);
	case 'c':	/* committer ... */
		return format_person_part(sb, placeholder[1],
		                   msg + c->committer.off, c->committer.len);
	case 'e':	/* encoding */
		strbuf_add(sb, msg + c->encoding.off, c->encoding.len);
		return 1;
	case 'b':	/* body */
		strbuf_addstr(sb, msg + c->body_off);
		return 1;
	}
	return 0;	/* unknown placeholder */
}

void format_commit_message(const struct commit *commit,
                           const void *format, struct strbuf *sb)
{
	struct format_commit_context context;

	memset(&context, 0, sizeof(context));
	context.commit = commit;
	strbuf_expand(sb, format, format_commit_item, &context);
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
			pp_user_info("Author", fmt, sb, line + 7, dmode, encoding);
		}
		if (!memcmp(line, "committer ", 10) &&
		    (fmt == CMIT_FMT_FULL || fmt == CMIT_FMT_FULLER)) {
			strbuf_grow(sb, linelen + 80);
			pp_user_info("Commit", fmt, sb, line + 10, dmode, encoding);
		}
	}
}

void pp_title_line(enum cmit_fmt fmt,
		   const char **msg_p,
		   struct strbuf *sb,
		   const char *subject,
		   const char *after_subject,
		   const char *encoding,
		   int need_8bit_cte)
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

	if (need_8bit_cte > 0) {
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

void pp_remainder(enum cmit_fmt fmt,
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
			 enum date_mode dmode, int need_8bit_cte)
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

	/*
	 * We need to check and emit Content-type: to mark it
	 * as 8-bit if we haven't done so.
	 */
	if (fmt == CMIT_FMT_EMAIL && need_8bit_cte == 0) {
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
				need_8bit_cte = 1;
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
			      after_subject, encoding, need_8bit_cte);

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
