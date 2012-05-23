#include "cache.h"
#include "commit.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"
#include "string-list.h"
#include "mailmap.h"
#include "log-tree.h"
#include "notes.h"
#include "color.h"
#include "reflog-walk.h"
#include "gpg-interface.h"

static char *user_format;
static struct cmt_fmt_map {
	const char *name;
	enum cmit_fmt format;
	int is_tformat;
	int is_alias;
	const char *user_format;
} *commit_formats;
static size_t builtin_formats_len;
static size_t commit_formats_len;
static size_t commit_formats_alloc;
static struct cmt_fmt_map *find_commit_format(const char *sought);

static void save_user_format(struct rev_info *rev, const char *cp, int is_tformat)
{
	free(user_format);
	user_format = xstrdup(cp);
	if (is_tformat)
		rev->use_terminator = 1;
	rev->commit_format = CMIT_FMT_USERFORMAT;
}

static int git_pretty_formats_config(const char *var, const char *value, void *cb)
{
	struct cmt_fmt_map *commit_format = NULL;
	const char *name;
	const char *fmt;
	int i;

	if (prefixcmp(var, "pretty."))
		return 0;

	name = var + strlen("pretty.");
	for (i = 0; i < builtin_formats_len; i++) {
		if (!strcmp(commit_formats[i].name, name))
			return 0;
	}

	for (i = builtin_formats_len; i < commit_formats_len; i++) {
		if (!strcmp(commit_formats[i].name, name)) {
			commit_format = &commit_formats[i];
			break;
		}
	}

	if (!commit_format) {
		ALLOC_GROW(commit_formats, commit_formats_len+1,
			   commit_formats_alloc);
		commit_format = &commit_formats[commit_formats_len];
		memset(commit_format, 0, sizeof(*commit_format));
		commit_formats_len++;
	}

	commit_format->name = xstrdup(name);
	commit_format->format = CMIT_FMT_USERFORMAT;
	git_config_string(&fmt, var, value);
	if (!prefixcmp(fmt, "format:") || !prefixcmp(fmt, "tformat:")) {
		commit_format->is_tformat = fmt[0] == 't';
		fmt = strchr(fmt, ':') + 1;
	} else if (strchr(fmt, '%'))
		commit_format->is_tformat = 1;
	else
		commit_format->is_alias = 1;
	commit_format->user_format = fmt;

	return 0;
}

static void setup_commit_formats(void)
{
	struct cmt_fmt_map builtin_formats[] = {
		{ "raw",	CMIT_FMT_RAW,		0 },
		{ "medium",	CMIT_FMT_MEDIUM,	0 },
		{ "short",	CMIT_FMT_SHORT,		0 },
		{ "email",	CMIT_FMT_EMAIL,		0 },
		{ "fuller",	CMIT_FMT_FULLER,	0 },
		{ "full",	CMIT_FMT_FULL,		0 },
		{ "oneline",	CMIT_FMT_ONELINE,	1 }
	};
	commit_formats_len = ARRAY_SIZE(builtin_formats);
	builtin_formats_len = commit_formats_len;
	ALLOC_GROW(commit_formats, commit_formats_len, commit_formats_alloc);
	memcpy(commit_formats, builtin_formats,
	       sizeof(*builtin_formats)*ARRAY_SIZE(builtin_formats));

	git_config(git_pretty_formats_config, NULL);
}

static struct cmt_fmt_map *find_commit_format_recursive(const char *sought,
							const char *original,
							int num_redirections)
{
	struct cmt_fmt_map *found = NULL;
	size_t found_match_len = 0;
	int i;

	if (num_redirections >= commit_formats_len)
		die("invalid --pretty format: "
		    "'%s' references an alias which points to itself",
		    original);

	for (i = 0; i < commit_formats_len; i++) {
		size_t match_len;

		if (prefixcmp(commit_formats[i].name, sought))
			continue;

		match_len = strlen(commit_formats[i].name);
		if (found == NULL || found_match_len > match_len) {
			found = &commit_formats[i];
			found_match_len = match_len;
		}
	}

	if (found && found->is_alias) {
		found = find_commit_format_recursive(found->user_format,
						     original,
						     num_redirections+1);
	}

	return found;
}

static struct cmt_fmt_map *find_commit_format(const char *sought)
{
	if (!commit_formats)
		setup_commit_formats();

	return find_commit_format_recursive(sought, sought, 0);
}

void get_commit_format(const char *arg, struct rev_info *rev)
{
	struct cmt_fmt_map *commit_format;

	rev->use_terminator = 0;
	if (!arg || !*arg) {
		rev->commit_format = CMIT_FMT_DEFAULT;
		return;
	}
	if (!prefixcmp(arg, "format:") || !prefixcmp(arg, "tformat:")) {
		save_user_format(rev, strchr(arg, ':') + 1, arg[0] == 't');
		return;
	}

	if (strchr(arg, '%')) {
		save_user_format(rev, arg, 1);
		return;
	}

	commit_format = find_commit_format(arg);
	if (!commit_format)
		die("invalid --pretty format: %s", arg);

	rev->commit_format = commit_format->format;
	rev->use_terminator = commit_format->is_tformat;
	if (commit_format->format == CMIT_FMT_USERFORMAT) {
		save_user_format(rev, commit_format->user_format,
				 commit_format->is_tformat);
	}
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
static int non_ascii(int ch)
{
	return !isascii(ch) || ch == '\033';
}

int has_non_ascii(const char *s)
{
	int ch;
	if (!s)
		return 0;
	while ((ch = *s++) != '\0') {
		if (non_ascii(ch))
			return 1;
	}
	return 0;
}

static int is_rfc822_special(char ch)
{
	switch (ch) {
	case '(':
	case ')':
	case '<':
	case '>':
	case '[':
	case ']':
	case ':':
	case ';':
	case '@':
	case ',':
	case '.':
	case '"':
	case '\\':
		return 1;
	default:
		return 0;
	}
}

static int has_rfc822_specials(const char *s, int len)
{
	int i;
	for (i = 0; i < len; i++)
		if (is_rfc822_special(s[i]))
			return 1;
	return 0;
}

static void add_rfc822_quoted(struct strbuf *out, const char *s, int len)
{
	int i;

	/* just a guess, we may have to also backslash-quote */
	strbuf_grow(out, len + 2);

	strbuf_addch(out, '"');
	for (i = 0; i < len; i++) {
		switch (s[i]) {
		case '"':
		case '\\':
			strbuf_addch(out, '\\');
			/* fall through */
		default:
			strbuf_addch(out, s[i]);
		}
	}
	strbuf_addch(out, '"');
}

static int is_rfc2047_special(char ch)
{
	return (non_ascii(ch) || (ch == '=') || (ch == '?') || (ch == '_'));
}

static void add_rfc2047(struct strbuf *sb, const char *line, int len,
		       const char *encoding)
{
	static const int max_length = 78; /* per rfc2822 */
	int i;
	int line_len;

	/* How many bytes are already used on the current line? */
	for (i = sb->len - 1; i >= 0; i--)
		if (sb->buf[i] == '\n')
			break;
	line_len = sb->len - (i+1);

	for (i = 0; i < len; i++) {
		int ch = line[i];
		if (non_ascii(ch) || ch == '\n')
			goto needquote;
		if ((i + 1 < len) && (ch == '=' && line[i+1] == '?'))
			goto needquote;
	}
	strbuf_add_wrapped_bytes(sb, line, len, 0, 1, max_length - line_len);
	return;

needquote:
	strbuf_grow(sb, len * 3 + strlen(encoding) + 100);
	strbuf_addf(sb, "=?%s?q?", encoding);
	line_len += strlen(encoding) + 5; /* 5 for =??q? */
	for (i = 0; i < len; i++) {
		unsigned ch = line[i] & 0xFF;

		if (line_len >= max_length - 2) {
			strbuf_addf(sb, "?=\n =?%s?q?", encoding);
			line_len = strlen(encoding) + 5 + 1; /* =??q? plus SP */
		}

		/*
		 * We encode ' ' using '=20' even though rfc2047
		 * allows using '_' for readability.  Unfortunately,
		 * many programs do not understand this and just
		 * leave the underscore in place.
		 */
		if (is_rfc2047_special(ch) || ch == ' ' || ch == '\n') {
			strbuf_addf(sb, "=%02X", ch);
			line_len += 3;
		}
		else {
			strbuf_addch(sb, ch);
			line_len++;
		}
	}
	strbuf_addstr(sb, "?=");
}

void pp_user_info(const struct pretty_print_context *pp,
		  const char *what, struct strbuf *sb,
		  const char *line, const char *encoding)
{
	char *date;
	int namelen;
	unsigned long time;
	int tz;

	if (pp->fmt == CMIT_FMT_ONELINE)
		return;
	date = strchr(line, '>');
	if (!date)
		return;
	namelen = ++date - line;
	time = strtoul(date, &date, 10);
	tz = strtol(date, NULL, 10);

	if (pp->fmt == CMIT_FMT_EMAIL) {
		char *name_tail = strchr(line, '<');
		int display_name_length;
		int final_line;
		if (!name_tail)
			return;
		while (line < name_tail && isspace(name_tail[-1]))
			name_tail--;
		display_name_length = name_tail - line;
		strbuf_addstr(sb, "From: ");
		if (!has_rfc822_specials(line, display_name_length)) {
			add_rfc2047(sb, line, display_name_length, encoding);
		} else {
			struct strbuf quoted = STRBUF_INIT;
			add_rfc822_quoted(&quoted, line, display_name_length);
			add_rfc2047(sb, quoted.buf, quoted.len, encoding);
			strbuf_release(&quoted);
		}
		for (final_line = 0; final_line < sb->len; final_line++)
			if (sb->buf[sb->len - final_line - 1] == '\n')
				break;
		if (namelen - display_name_length + final_line > 78) {
			strbuf_addch(sb, '\n');
			if (!isspace(name_tail[0]))
				strbuf_addch(sb, ' ');
		}
		strbuf_add(sb, name_tail, namelen - display_name_length);
		strbuf_addch(sb, '\n');
	} else {
		strbuf_addf(sb, "%s: %.*s%.*s\n", what,
			      (pp->fmt == CMIT_FMT_FULLER) ? 4 : 0,
			      "    ", namelen, line);
	}
	switch (pp->fmt) {
	case CMIT_FMT_MEDIUM:
		strbuf_addf(sb, "Date:   %s\n", show_date(time, tz, pp->date_mode));
		break;
	case CMIT_FMT_EMAIL:
		strbuf_addf(sb, "Date: %s\n", show_date(time, tz, DATE_RFC2822));
		break;
	case CMIT_FMT_FULLER:
		strbuf_addf(sb, "%sDate: %s\n", what, show_date(time, tz, pp->date_mode));
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

static const char *skip_empty_lines(const char *msg)
{
	for (;;) {
		int linelen = get_one_line(msg);
		int ll = linelen;
		if (!linelen)
			break;
		if (!is_empty_line(msg, &ll))
			break;
		msg += linelen;
	}
	return msg;
}

static void add_merge_info(const struct pretty_print_context *pp,
			   struct strbuf *sb, const struct commit *commit)
{
	struct commit_list *parent = commit->parents;

	if ((pp->fmt == CMIT_FMT_ONELINE) || (pp->fmt == CMIT_FMT_EMAIL) ||
	    !parent || !parent->next)
		return;

	strbuf_addstr(sb, "Merge:");

	while (parent) {
		struct commit *p = parent->item;
		const char *hex = NULL;
		if (pp->abbrev)
			hex = find_unique_abbrev(p->object.sha1, pp->abbrev);
		if (!hex)
			hex = sha1_to_hex(p->object.sha1);
		parent = parent->next;

		strbuf_addf(sb, " %s", hex);
	}
	strbuf_addch(sb, '\n');
}

static char *get_header(const struct commit *commit, const char *key)
{
	int key_len = strlen(key);
	const char *line = commit->buffer;

	while (line) {
		const char *eol = strchr(line, '\n'), *next;

		if (line == eol)
			return NULL;
		if (!eol) {
			warning("malformed commit (header is missing newline): %s",
				sha1_to_hex(commit->object.sha1));
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
	return NULL;
}

static char *replace_encoding_header(char *buf, const char *encoding)
{
	struct strbuf tmp = STRBUF_INIT;
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

char *logmsg_reencode(const struct commit *commit,
		      const char *output_encoding)
{
	static const char *utf8 = "UTF-8";
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

static int mailmap_name(char *email, int email_len, char *name, int name_len)
{
	static struct string_list *mail_map;
	if (!mail_map) {
		mail_map = xcalloc(1, sizeof(*mail_map));
		read_mailmap(mail_map, NULL);
	}
	return mail_map->nr && map_user(mail_map, email, email_len, name, name_len);
}

static size_t format_person_part(struct strbuf *sb, char part,
				 const char *msg, int len, enum date_mode dmode)
{
	/* currently all placeholders have same length */
	const int placeholder_len = 2;
	int tz;
	unsigned long date = 0;
	char person_name[1024];
	char person_mail[1024];
	struct ident_split s;
	const char *name_start, *name_end, *mail_start, *mail_end;

	if (split_ident_line(&s, msg, len) < 0)
		goto skip;

	name_start = s.name_begin;
	name_end = s.name_end;
	mail_start = s.mail_begin;
	mail_end = s.mail_end;

	if (part == 'N' || part == 'E') { /* mailmap lookup */
		snprintf(person_name, sizeof(person_name), "%.*s",
			 (int)(name_end - name_start), name_start);
		snprintf(person_mail, sizeof(person_mail), "%.*s",
			 (int)(mail_end - mail_start), mail_start);
		mailmap_name(person_mail, sizeof(person_mail), person_name, sizeof(person_name));
		name_start = person_name;
		name_end = name_start + strlen(person_name);
		mail_start = person_mail;
		mail_end = mail_start +  strlen(person_mail);
	}
	if (part == 'n' || part == 'N') {	/* name */
		strbuf_add(sb, name_start, name_end-name_start);
		return placeholder_len;
	}
	if (part == 'e' || part == 'E') {	/* email */
		strbuf_add(sb, mail_start, mail_end-mail_start);
		return placeholder_len;
	}

	if (!s.date_begin)
		goto skip;

	date = strtoul(s.date_begin, NULL, 10);

	if (part == 't') {	/* date, UNIX timestamp */
		strbuf_add(sb, s.date_begin, s.date_end - s.date_begin);
		return placeholder_len;
	}

	/* parse tz */
	tz = strtoul(s.tz_begin + 1, NULL, 10);
	if (*s.tz_begin == '-')
		tz = -tz;

	switch (part) {
	case 'd':	/* date */
		strbuf_addstr(sb, show_date(date, tz, dmode));
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
	 * reading from either a bogus commit, or a reflog entry with
	 * %gn, %ge, etc.; 'sb' cannot be updated, but we still need
	 * to compute a valid return value.
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
	const struct pretty_print_context *pretty_ctx;
	unsigned commit_header_parsed:1;
	unsigned commit_message_parsed:1;
	unsigned commit_signature_parsed:1;
	struct {
		char *gpg_output;
		char good_bad;
		char *signer;
	} signature;
	char *message;
	size_t width, indent1, indent2;

	/* These offsets are relative to the start of the commit message. */
	struct chunk author;
	struct chunk committer;
	struct chunk encoding;
	size_t message_off;
	size_t subject_off;
	size_t body_off;

	/* The following ones are relative to the result struct strbuf. */
	struct chunk abbrev_commit_hash;
	struct chunk abbrev_tree_hash;
	struct chunk abbrev_parent_hashes;
	size_t wrap_start;
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
	const char *msg = context->message;
	int i;

	for (i = 0; msg[i]; i++) {
		int eol;
		for (eol = i; msg[eol] && msg[eol] != '\n'; eol++)
			; /* do nothing */

		if (i == eol) {
			break;
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
	}
	context->message_off = i;
	context->commit_header_parsed = 1;
}

static int istitlechar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '.' || c == '_';
}

static void format_sanitized_subject(struct strbuf *sb, const char *msg)
{
	size_t trimlen;
	size_t start_len = sb->len;
	int space = 2;

	for (; *msg && *msg != '\n'; msg++) {
		if (istitlechar(*msg)) {
			if (space == 1)
				strbuf_addch(sb, '-');
			space = 0;
			strbuf_addch(sb, *msg);
			if (*msg == '.')
				while (*(msg+1) == '.')
					msg++;
		} else
			space |= 1;
	}

	/* trim any trailing '.' or '-' characters */
	trimlen = 0;
	while (sb->len - trimlen > start_len &&
		(sb->buf[sb->len - 1 - trimlen] == '.'
		|| sb->buf[sb->len - 1 - trimlen] == '-'))
		trimlen++;
	strbuf_remove(sb, sb->len - trimlen, trimlen);
}

const char *format_subject(struct strbuf *sb, const char *msg,
			   const char *line_separator)
{
	int first = 1;

	for (;;) {
		const char *line = msg;
		int linelen = get_one_line(line);

		msg += linelen;
		if (!linelen || is_empty_line(line, &linelen))
			break;

		if (!sb)
			continue;
		strbuf_grow(sb, linelen + 2);
		if (!first)
			strbuf_addstr(sb, line_separator);
		strbuf_add(sb, line, linelen);
		first = 0;
	}
	return msg;
}

static void parse_commit_message(struct format_commit_context *c)
{
	const char *msg = c->message + c->message_off;
	const char *start = c->message;

	msg = skip_empty_lines(msg);
	c->subject_off = msg - start;

	msg = format_subject(NULL, msg, NULL);
	msg = skip_empty_lines(msg);
	c->body_off = msg - start;

	c->commit_message_parsed = 1;
}

static void format_decoration(struct strbuf *sb, const struct commit *commit)
{
	struct name_decoration *d;
	const char *prefix = " (";

	load_ref_decorations(DECORATE_SHORT_REFS);
	d = lookup_decoration(&name_decoration, &commit->object);
	while (d) {
		strbuf_addstr(sb, prefix);
		prefix = ", ";
		strbuf_addstr(sb, d->name);
		d = d->next;
	}
	if (prefix[0] == ',')
		strbuf_addch(sb, ')');
}

static void strbuf_wrap(struct strbuf *sb, size_t pos,
			size_t width, size_t indent1, size_t indent2)
{
	struct strbuf tmp = STRBUF_INIT;

	if (pos)
		strbuf_add(&tmp, sb->buf, pos);
	strbuf_add_wrapped_text(&tmp, sb->buf + pos,
				(int) indent1, (int) indent2, (int) width);
	strbuf_swap(&tmp, sb);
	strbuf_release(&tmp);
}

static void rewrap_message_tail(struct strbuf *sb,
				struct format_commit_context *c,
				size_t new_width, size_t new_indent1,
				size_t new_indent2)
{
	if (c->width == new_width && c->indent1 == new_indent1 &&
	    c->indent2 == new_indent2)
		return;
	if (c->wrap_start < sb->len)
		strbuf_wrap(sb, c->wrap_start, c->width, c->indent1, c->indent2);
	c->wrap_start = sb->len;
	c->width = new_width;
	c->indent1 = new_indent1;
	c->indent2 = new_indent2;
}

static struct {
	char result;
	const char *check;
} signature_check[] = {
	{ 'G', ": Good signature from " },
	{ 'B', ": BAD signature from " },
};

static void parse_signature_lines(struct format_commit_context *ctx)
{
	const char *buf = ctx->signature.gpg_output;
	int i;

	for (i = 0; i < ARRAY_SIZE(signature_check); i++) {
		const char *found = strstr(buf, signature_check[i].check);
		const char *next;
		if (!found)
			continue;
		ctx->signature.good_bad = signature_check[i].result;
		found += strlen(signature_check[i].check);
		next = strchrnul(found, '\n');
		ctx->signature.signer = xmemdupz(found, next - found);
		break;
	}
}

static void parse_commit_signature(struct format_commit_context *ctx)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	struct strbuf gpg_output = STRBUF_INIT;
	int status;

	ctx->commit_signature_parsed = 1;

	if (parse_signed_commit(ctx->commit->object.sha1,
				&payload, &signature) <= 0)
		goto out;
	status = verify_signed_buffer(payload.buf, payload.len,
				      signature.buf, signature.len,
				      &gpg_output);
	if (status && !gpg_output.len)
		goto out;
	ctx->signature.gpg_output = strbuf_detach(&gpg_output, NULL);
	parse_signature_lines(ctx);

 out:
	strbuf_release(&gpg_output);
	strbuf_release(&payload);
	strbuf_release(&signature);
}


static int format_reflog_person(struct strbuf *sb,
				char part,
				struct reflog_walk_info *log,
				enum date_mode dmode)
{
	const char *ident;

	if (!log)
		return 2;

	ident = get_reflog_ident(log);
	if (!ident)
		return 2;

	return format_person_part(sb, part, ident, strlen(ident), dmode);
}

static size_t format_commit_one(struct strbuf *sb, const char *placeholder,
				void *context)
{
	struct format_commit_context *c = context;
	const struct commit *commit = c->commit;
	const char *msg = c->message;
	struct commit_list *p;
	int h1, h2;

	/* these are independent of the commit */
	switch (placeholder[0]) {
	case 'C':
		if (placeholder[1] == '(') {
			const char *end = strchr(placeholder + 2, ')');
			char color[COLOR_MAXLEN];
			if (!end)
				return 0;
			color_parse_mem(placeholder + 2,
					end - (placeholder + 2),
					"--pretty format", color);
			strbuf_addstr(sb, color);
			return end - placeholder + 1;
		}
		if (!prefixcmp(placeholder + 1, "red")) {
			strbuf_addstr(sb, GIT_COLOR_RED);
			return 4;
		} else if (!prefixcmp(placeholder + 1, "green")) {
			strbuf_addstr(sb, GIT_COLOR_GREEN);
			return 6;
		} else if (!prefixcmp(placeholder + 1, "blue")) {
			strbuf_addstr(sb, GIT_COLOR_BLUE);
			return 5;
		} else if (!prefixcmp(placeholder + 1, "reset")) {
			strbuf_addstr(sb, GIT_COLOR_RESET);
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
	case 'w':
		if (placeholder[1] == '(') {
			unsigned long width = 0, indent1 = 0, indent2 = 0;
			char *next;
			const char *start = placeholder + 2;
			const char *end = strchr(start, ')');
			if (!end)
				return 0;
			if (end > start) {
				width = strtoul(start, &next, 10);
				if (*next == ',') {
					indent1 = strtoul(next + 1, &next, 10);
					if (*next == ',') {
						indent2 = strtoul(next + 1,
								 &next, 10);
					}
				}
				if (*next != ')')
					return 0;
			}
			rewrap_message_tail(sb, c, width, indent1, indent2);
			return end - placeholder + 1;
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
						     c->pretty_ctx->abbrev));
		c->abbrev_commit_hash.len = sb->len - c->abbrev_commit_hash.off;
		return 1;
	case 'T':		/* tree hash */
		strbuf_addstr(sb, sha1_to_hex(commit->tree->object.sha1));
		return 1;
	case 't':		/* abbreviated tree hash */
		if (add_again(sb, &c->abbrev_tree_hash))
			return 1;
		strbuf_addstr(sb, find_unique_abbrev(commit->tree->object.sha1,
						     c->pretty_ctx->abbrev));
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
					p->item->object.sha1,
					c->pretty_ctx->abbrev));
		}
		c->abbrev_parent_hashes.len = sb->len -
		                              c->abbrev_parent_hashes.off;
		return 1;
	case 'm':		/* left/right/bottom */
		strbuf_addstr(sb, get_revision_mark(NULL, commit));
		return 1;
	case 'd':
		format_decoration(sb, commit);
		return 1;
	case 'g':		/* reflog info */
		switch(placeholder[1]) {
		case 'd':	/* reflog selector */
		case 'D':
			if (c->pretty_ctx->reflog_info)
				get_reflog_selector(sb,
						    c->pretty_ctx->reflog_info,
						    c->pretty_ctx->date_mode,
						    c->pretty_ctx->date_mode_explicit,
						    (placeholder[1] == 'd'));
			return 2;
		case 's':	/* reflog message */
			if (c->pretty_ctx->reflog_info)
				get_reflog_message(sb, c->pretty_ctx->reflog_info);
			return 2;
		case 'n':
		case 'N':
		case 'e':
		case 'E':
			return format_reflog_person(sb,
						    placeholder[1],
						    c->pretty_ctx->reflog_info,
						    c->pretty_ctx->date_mode);
		}
		return 0;	/* unknown %g placeholder */
	case 'N':
		if (c->pretty_ctx->show_notes) {
			format_display_notes(commit->object.sha1, sb,
				    get_log_output_encoding(), 0);
			return 1;
		}
		return 0;
	}

	if (placeholder[0] == 'G') {
		if (!c->commit_signature_parsed)
			parse_commit_signature(c);
		switch (placeholder[1]) {
		case 'G':
			if (c->signature.gpg_output)
				strbuf_addstr(sb, c->signature.gpg_output);
			break;
		case '?':
			switch (c->signature.good_bad) {
			case 'G':
			case 'B':
				strbuf_addch(sb, c->signature.good_bad);
			}
			break;
		case 'S':
			if (c->signature.signer)
				strbuf_addstr(sb, c->signature.signer);
			break;
		}
		return 2;
	}


	/* For the rest we have to parse the commit header. */
	if (!c->commit_header_parsed)
		parse_commit_header(c);

	switch (placeholder[0]) {
	case 'a':	/* author ... */
		return format_person_part(sb, placeholder[1],
				   msg + c->author.off, c->author.len,
				   c->pretty_ctx->date_mode);
	case 'c':	/* committer ... */
		return format_person_part(sb, placeholder[1],
				   msg + c->committer.off, c->committer.len,
				   c->pretty_ctx->date_mode);
	case 'e':	/* encoding */
		strbuf_add(sb, msg + c->encoding.off, c->encoding.len);
		return 1;
	case 'B':	/* raw body */
		/* message_off is always left at the initial newline */
		strbuf_addstr(sb, msg + c->message_off + 1);
		return 1;
	}

	/* Now we need to parse the commit message. */
	if (!c->commit_message_parsed)
		parse_commit_message(c);

	switch (placeholder[0]) {
	case 's':	/* subject */
		format_subject(sb, msg + c->subject_off, " ");
		return 1;
	case 'f':	/* sanitized subject */
		format_sanitized_subject(sb, msg + c->subject_off);
		return 1;
	case 'b':	/* body */
		strbuf_addstr(sb, msg + c->body_off);
		return 1;
	}
	return 0;	/* unknown placeholder */
}

static size_t format_commit_item(struct strbuf *sb, const char *placeholder,
				 void *context)
{
	int consumed;
	size_t orig_len;
	enum {
		NO_MAGIC,
		ADD_LF_BEFORE_NON_EMPTY,
		DEL_LF_BEFORE_EMPTY,
		ADD_SP_BEFORE_NON_EMPTY
	} magic = NO_MAGIC;

	switch (placeholder[0]) {
	case '-':
		magic = DEL_LF_BEFORE_EMPTY;
		break;
	case '+':
		magic = ADD_LF_BEFORE_NON_EMPTY;
		break;
	case ' ':
		magic = ADD_SP_BEFORE_NON_EMPTY;
		break;
	default:
		break;
	}
	if (magic != NO_MAGIC)
		placeholder++;

	orig_len = sb->len;
	consumed = format_commit_one(sb, placeholder, context);
	if (magic == NO_MAGIC)
		return consumed;

	if ((orig_len == sb->len) && magic == DEL_LF_BEFORE_EMPTY) {
		while (sb->len && sb->buf[sb->len - 1] == '\n')
			strbuf_setlen(sb, sb->len - 1);
	} else if (orig_len != sb->len) {
		if (magic == ADD_LF_BEFORE_NON_EMPTY)
			strbuf_insert(sb, orig_len, "\n", 1);
		else if (magic == ADD_SP_BEFORE_NON_EMPTY)
			strbuf_insert(sb, orig_len, " ", 1);
	}
	return consumed + 1;
}

static size_t userformat_want_item(struct strbuf *sb, const char *placeholder,
				   void *context)
{
	struct userformat_want *w = context;

	if (*placeholder == '+' || *placeholder == '-' || *placeholder == ' ')
		placeholder++;

	switch (*placeholder) {
	case 'N':
		w->notes = 1;
		break;
	}
	return 0;
}

void userformat_find_requirements(const char *fmt, struct userformat_want *w)
{
	struct strbuf dummy = STRBUF_INIT;

	if (!fmt) {
		if (!user_format)
			return;
		fmt = user_format;
	}
	strbuf_expand(&dummy, fmt, userformat_want_item, w);
	strbuf_release(&dummy);
}

void format_commit_message(const struct commit *commit,
			   const char *format, struct strbuf *sb,
			   const struct pretty_print_context *pretty_ctx)
{
	struct format_commit_context context;
	static const char utf8[] = "UTF-8";
	const char *output_enc = pretty_ctx->output_encoding;

	memset(&context, 0, sizeof(context));
	context.commit = commit;
	context.pretty_ctx = pretty_ctx;
	context.wrap_start = sb->len;
	context.message = commit->buffer;
	if (output_enc) {
		char *enc = get_header(commit, "encoding");
		if (strcmp(enc ? enc : utf8, output_enc)) {
			context.message = logmsg_reencode(commit, output_enc);
			if (!context.message)
				context.message = commit->buffer;
		}
		free(enc);
	}

	strbuf_expand(sb, format, format_commit_item, &context);
	rewrap_message_tail(sb, &context, 0, 0, 0);

	if (context.message != commit->buffer)
		free(context.message);
	free(context.signature.gpg_output);
	free(context.signature.signer);
}

static void pp_header(const struct pretty_print_context *pp,
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

		if (pp->fmt == CMIT_FMT_RAW) {
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
			add_merge_info(pp, sb, commit);
			parents_shown = 1;
		}

		/*
		 * MEDIUM == DEFAULT shows only author with dates.
		 * FULL shows both authors but not dates.
		 * FULLER shows both authors and dates.
		 */
		if (!memcmp(line, "author ", 7)) {
			strbuf_grow(sb, linelen + 80);
			pp_user_info(pp, "Author", sb, line + 7, encoding);
		}
		if (!memcmp(line, "committer ", 10) &&
		    (pp->fmt == CMIT_FMT_FULL || pp->fmt == CMIT_FMT_FULLER)) {
			strbuf_grow(sb, linelen + 80);
			pp_user_info(pp, "Commit", sb, line + 10, encoding);
		}
	}
}

void pp_title_line(const struct pretty_print_context *pp,
		   const char **msg_p,
		   struct strbuf *sb,
		   const char *encoding,
		   int need_8bit_cte)
{
	struct strbuf title;

	strbuf_init(&title, 80);
	*msg_p = format_subject(&title, *msg_p,
				pp->preserve_subject ? "\n" : " ");

	strbuf_grow(sb, title.len + 1024);
	if (pp->subject) {
		strbuf_addstr(sb, pp->subject);
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
	if (pp->after_subject) {
		strbuf_addstr(sb, pp->after_subject);
	}
	if (pp->fmt == CMIT_FMT_EMAIL) {
		strbuf_addch(sb, '\n');
	}
	strbuf_release(&title);
}

void pp_remainder(const struct pretty_print_context *pp,
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
			if (pp->fmt == CMIT_FMT_SHORT)
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

char *reencode_commit_message(const struct commit *commit, const char **encoding_p)
{
	const char *encoding;

	encoding = get_log_output_encoding();
	if (encoding_p)
		*encoding_p = encoding;
	return logmsg_reencode(commit, encoding);
}

void pretty_print_commit(const struct pretty_print_context *pp,
			 const struct commit *commit,
			 struct strbuf *sb)
{
	unsigned long beginning_of_body;
	int indent = 4;
	const char *msg = commit->buffer;
	char *reencoded;
	const char *encoding;
	int need_8bit_cte = pp->need_8bit_cte;

	if (pp->fmt == CMIT_FMT_USERFORMAT) {
		format_commit_message(commit, user_format, sb, pp);
		return;
	}

	reencoded = reencode_commit_message(commit, &encoding);
	if (reencoded) {
		msg = reencoded;
	}

	if (pp->fmt == CMIT_FMT_ONELINE || pp->fmt == CMIT_FMT_EMAIL)
		indent = 0;

	/*
	 * We need to check and emit Content-type: to mark it
	 * as 8-bit if we haven't done so.
	 */
	if (pp->fmt == CMIT_FMT_EMAIL && need_8bit_cte == 0) {
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

	pp_header(pp, encoding, commit, &msg, sb);
	if (pp->fmt != CMIT_FMT_ONELINE && !pp->subject) {
		strbuf_addch(sb, '\n');
	}

	/* Skip excess blank lines at the beginning of body, if any... */
	msg = skip_empty_lines(msg);

	/* These formats treat the title line specially. */
	if (pp->fmt == CMIT_FMT_ONELINE || pp->fmt == CMIT_FMT_EMAIL)
		pp_title_line(pp, &msg, sb, encoding, need_8bit_cte);

	beginning_of_body = sb->len;
	if (pp->fmt != CMIT_FMT_ONELINE)
		pp_remainder(pp, &msg, sb, indent);
	strbuf_rtrim(sb);

	/* Make sure there is an EOLN for the non-oneline case */
	if (pp->fmt != CMIT_FMT_ONELINE)
		strbuf_addch(sb, '\n');

	/*
	 * The caller may append additional body text in e-mail
	 * format.  Make sure we did not strip the blank line
	 * between the header and the body.
	 */
	if (pp->fmt == CMIT_FMT_EMAIL && sb->len <= beginning_of_body)
		strbuf_addch(sb, '\n');

	if (pp->show_notes)
		format_display_notes(commit->object.sha1, sb, encoding,
				     NOTES_SHOW_HEADER | NOTES_INDENT);

	free(reencoded);
}

void pp_commit_easy(enum cmit_fmt fmt, const struct commit *commit,
		    struct strbuf *sb)
{
	struct pretty_print_context pp = {0};
	pp.fmt = fmt;
	pretty_print_commit(&pp, commit, sb);
}
