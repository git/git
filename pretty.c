#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "utf8.h"
#include "diff.h"
#include "pager.h"
#include "revision.h"
#include "string-list.h"
#include "mailmap.h"
#include "log-tree.h"
#include "notes.h"
#include "color.h"
#include "reflog-walk.h"
#include "gpg-interface.h"
#include "trailer.h"
#include "run-command.h"
#include "object-name.h"

/*
 * The limit for formatting directives, which enable the caller to append
 * arbitrarily many bytes to the formatted buffer. This includes padding
 * and wrapping formatters.
 */
#define FORMATTING_LIMIT (16 * 1024)

static char *user_format;
static struct cmt_fmt_map {
	const char *name;
	enum cmit_fmt format;
	int is_tformat;
	int expand_tabs_in_log;
	int is_alias;
	enum date_mode_type default_date_mode_type;
	const char *user_format;
} *commit_formats;
static size_t builtin_formats_len;
static size_t commit_formats_len;
static size_t commit_formats_alloc;
static struct cmt_fmt_map *find_commit_format(const char *sought);

int commit_format_is_empty(enum cmit_fmt fmt)
{
	return fmt == CMIT_FMT_USERFORMAT && !*user_format;
}

static void save_user_format(struct rev_info *rev, const char *cp, int is_tformat)
{
	free(user_format);
	user_format = xstrdup(cp);
	if (is_tformat)
		rev->use_terminator = 1;
	rev->commit_format = CMIT_FMT_USERFORMAT;
}

static int git_pretty_formats_config(const char *var, const char *value,
				     const struct config_context *ctx UNUSED,
				     void *cb UNUSED)
{
	struct cmt_fmt_map *commit_format = NULL;
	const char *name, *stripped;
	char *fmt;
	int i;

	if (!skip_prefix(var, "pretty.", &name))
		return 0;

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

	free((char *)commit_format->name);
	commit_format->name = xstrdup(name);
	commit_format->format = CMIT_FMT_USERFORMAT;
	if (git_config_string(&fmt, var, value))
		return -1;

	free((char *)commit_format->user_format);
	if (skip_prefix(fmt, "format:", &stripped)) {
		commit_format->is_tformat = 0;
		commit_format->user_format = xstrdup(stripped);
		free(fmt);
	} else if (skip_prefix(fmt, "tformat:", &stripped)) {
		commit_format->is_tformat = 1;
		commit_format->user_format = xstrdup(stripped);
		free(fmt);
	} else if (strchr(fmt, '%')) {
		commit_format->is_tformat = 1;
		commit_format->user_format = fmt;
	} else {
		commit_format->is_alias = 1;
		commit_format->user_format = fmt;
	}

	return 0;
}

static void setup_commit_formats(void)
{
	struct cmt_fmt_map builtin_formats[] = {
		{ "raw",	CMIT_FMT_RAW,		0,	0 },
		{ "medium",	CMIT_FMT_MEDIUM,	0,	8 },
		{ "short",	CMIT_FMT_SHORT,		0,	0 },
		{ "email",	CMIT_FMT_EMAIL,		0,	0 },
		{ "mboxrd",	CMIT_FMT_MBOXRD,	0,	0 },
		{ "fuller",	CMIT_FMT_FULLER,	0,	8 },
		{ "full",	CMIT_FMT_FULL,		0,	8 },
		{ "oneline",	CMIT_FMT_ONELINE,	1,	0 },
		{ "reference",	CMIT_FMT_USERFORMAT,	1,	0,
			0, DATE_SHORT, "%C(auto)%h (%s, %ad)" },
		/*
		 * Please update $__git_log_pretty_formats in
		 * git-completion.bash when you add new formats.
		 */
	};
	commit_formats_len = ARRAY_SIZE(builtin_formats);
	builtin_formats_len = commit_formats_len;
	ALLOC_GROW(commit_formats, commit_formats_len, commit_formats_alloc);
	COPY_ARRAY(commit_formats, builtin_formats,
		   ARRAY_SIZE(builtin_formats));

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

		if (!istarts_with(commit_formats[i].name, sought))
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
	if (!arg) {
		rev->commit_format = CMIT_FMT_DEFAULT;
		return;
	}
	if (skip_prefix(arg, "format:", &arg)) {
		save_user_format(rev, arg, 0);
		return;
	}

	if (!*arg || skip_prefix(arg, "tformat:", &arg) || strchr(arg, '%')) {
		save_user_format(rev, arg, 1);
		return;
	}

	commit_format = find_commit_format(arg);
	if (!commit_format)
		die("invalid --pretty format: %s", arg);

	rev->commit_format = commit_format->format;
	rev->use_terminator = commit_format->is_tformat;
	rev->expand_tabs_in_log_default = commit_format->expand_tabs_in_log;
	if (!rev->date_mode_explicit && commit_format->default_date_mode_type)
		rev->date_mode.type = commit_format->default_date_mode_type;
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

static int needs_rfc822_quoting(const char *s, int len)
{
	int i;
	for (i = 0; i < len; i++)
		if (is_rfc822_special(s[i]))
			return 1;
	return 0;
}

static int last_line_length(struct strbuf *sb)
{
	int i;

	/* How many bytes are already used on the last line? */
	for (i = sb->len - 1; i >= 0; i--)
		if (sb->buf[i] == '\n')
			break;
	return sb->len - (i + 1);
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

enum rfc2047_type {
	RFC2047_SUBJECT,
	RFC2047_ADDRESS
};

static int is_rfc2047_special(char ch, enum rfc2047_type type)
{
	/*
	 * rfc2047, section 4.2:
	 *
	 *    8-bit values which correspond to printable ASCII characters other
	 *    than "=", "?", and "_" (underscore), MAY be represented as those
	 *    characters.  (But see section 5 for restrictions.)  In
	 *    particular, SPACE and TAB MUST NOT be represented as themselves
	 *    within encoded words.
	 */

	/*
	 * rule out non-ASCII characters and non-printable characters (the
	 * non-ASCII check should be redundant as isprint() is not localized
	 * and only knows about ASCII, but be defensive about that)
	 */
	if (non_ascii(ch) || !isprint(ch))
		return 1;

	/*
	 * rule out special printable characters (' ' should be the only
	 * whitespace character considered printable, but be defensive and use
	 * isspace())
	 */
	if (isspace(ch) || ch == '=' || ch == '?' || ch == '_')
		return 1;

	/*
	 * rfc2047, section 5.3:
	 *
	 *    As a replacement for a 'word' entity within a 'phrase', for example,
	 *    one that precedes an address in a From, To, or Cc header.  The ABNF
	 *    definition for 'phrase' from RFC 822 thus becomes:
	 *
	 *    phrase = 1*( encoded-word / word )
	 *
	 *    In this case the set of characters that may be used in a "Q"-encoded
	 *    'encoded-word' is restricted to: <upper and lower case ASCII
	 *    letters, decimal digits, "!", "*", "+", "-", "/", "=", and "_"
	 *    (underscore, ASCII 95.)>.  An 'encoded-word' that appears within a
	 *    'phrase' MUST be separated from any adjacent 'word', 'text' or
	 *    'special' by 'linear-white-space'.
	 */

	if (type != RFC2047_ADDRESS)
		return 0;

	/* '=' and '_' are special cases and have been checked above */
	return !(isalnum(ch) || ch == '!' || ch == '*' || ch == '+' || ch == '-' || ch == '/');
}

static int needs_rfc2047_encoding(const char *line, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int ch = line[i];
		if (non_ascii(ch) || ch == '\n')
			return 1;
		if ((i + 1 < len) && (ch == '=' && line[i+1] == '?'))
			return 1;
	}

	return 0;
}

static void add_rfc2047(struct strbuf *sb, const char *line, size_t len,
		       const char *encoding, enum rfc2047_type type)
{
	static const int max_encoded_length = 76; /* per rfc2047 */
	int i;
	int line_len = last_line_length(sb);

	strbuf_grow(sb, len * 3 + strlen(encoding) + 100);
	strbuf_addf(sb, "=?%s?q?", encoding);
	line_len += strlen(encoding) + 5; /* 5 for =??q? */

	while (len) {
		/*
		 * RFC 2047, section 5 (3):
		 *
		 * Each 'encoded-word' MUST represent an integral number of
		 * characters.  A multi-octet character may not be split across
		 * adjacent 'encoded- word's.
		 */
		const unsigned char *p = (const unsigned char *)line;
		int chrlen = mbs_chrlen(&line, &len, encoding);
		int is_special = (chrlen > 1) || is_rfc2047_special(*p, type);

		/* "=%02X" * chrlen, or the byte itself */
		const char *encoded_fmt = is_special ? "=%02X"    : "%c";
		int	    encoded_len = is_special ? 3 * chrlen : 1;

		/*
		 * According to RFC 2047, we could encode the special character
		 * ' ' (space) with '_' (underscore) for readability. But many
		 * programs do not understand this and just leave the
		 * underscore in place. Thus, we do nothing special here, which
		 * causes ' ' to be encoded as '=20', avoiding this problem.
		 */

		if (line_len + encoded_len + 2 > max_encoded_length) {
			/* It won't fit with trailing "?=" --- break the line */
			strbuf_addf(sb, "?=\n =?%s?q?", encoding);
			line_len = strlen(encoding) + 5 + 1; /* =??q? plus SP */
		}

		for (i = 0; i < chrlen; i++)
			strbuf_addf(sb, encoded_fmt, p[i]);
		line_len += encoded_len;
	}
	strbuf_addstr(sb, "?=");
}

const char *show_ident_date(const struct ident_split *ident,
			    struct date_mode mode)
{
	timestamp_t date = 0;
	long tz = 0;

	if (ident->date_begin && ident->date_end)
		date = parse_timestamp(ident->date_begin, NULL, 10);
	if (date_overflows(date))
		date = 0;
	else {
		if (ident->tz_begin && ident->tz_end)
			tz = strtol(ident->tz_begin, NULL, 10);
		if (tz >= INT_MAX || tz <= INT_MIN)
			tz = 0;
	}
	return show_date(date, tz, mode);
}

static inline void strbuf_add_with_color(struct strbuf *sb, const char *color,
					 const char *buf, size_t buflen)
{
	strbuf_addstr(sb, color);
	strbuf_add(sb, buf, buflen);
	if (*color)
		strbuf_addstr(sb, GIT_COLOR_RESET);
}

static void append_line_with_color(struct strbuf *sb, struct grep_opt *opt,
				   const char *line, size_t linelen,
				   int color, enum grep_context ctx,
				   enum grep_header_field field)
{
	const char *buf, *eol, *line_color, *match_color;
	regmatch_t match;
	int eflags = 0;

	buf = line;
	eol = buf + linelen;

	if (!opt || !want_color(color) || opt->invert)
		goto end;

	line_color = opt->colors[GREP_COLOR_SELECTED];
	match_color = opt->colors[GREP_COLOR_MATCH_SELECTED];

	while (grep_next_match(opt, buf, eol, ctx, &match, field, eflags)) {
		if (match.rm_so == match.rm_eo)
			break;

		strbuf_add_with_color(sb, line_color, buf, match.rm_so);
		strbuf_add_with_color(sb, match_color, buf + match.rm_so,
				      match.rm_eo - match.rm_so);
		buf += match.rm_eo;
		eflags = REG_NOTBOL;
	}

	if (eflags)
		strbuf_add_with_color(sb, line_color, buf, eol - buf);
	else {
end:
		strbuf_add(sb, buf, eol - buf);
	}
}

static int use_in_body_from(const struct pretty_print_context *pp,
			    const struct ident_split *ident)
{
	if (pp->rev && pp->rev->force_in_body_from)
		return 1;
	if (ident_cmp(pp->from_ident, ident))
		return 1;
	return 0;
}

void pp_user_info(struct pretty_print_context *pp,
		  const char *what, struct strbuf *sb,
		  const char *line, const char *encoding)
{
	struct ident_split ident;
	char *line_end;
	const char *mailbuf, *namebuf;
	size_t namelen, maillen;
	int max_length = 78; /* per rfc2822 */

	if (pp->fmt == CMIT_FMT_ONELINE)
		return;

	line_end = strchrnul(line, '\n');
	if (split_ident_line(&ident, line, line_end - line))
		return;

	mailbuf = ident.mail_begin;
	maillen = ident.mail_end - ident.mail_begin;
	namebuf = ident.name_begin;
	namelen = ident.name_end - ident.name_begin;

	if (pp->mailmap)
		map_user(pp->mailmap, &mailbuf, &maillen, &namebuf, &namelen);

	if (cmit_fmt_is_mail(pp->fmt)) {
		if (pp->from_ident && use_in_body_from(pp, &ident)) {
			struct strbuf buf = STRBUF_INIT;

			strbuf_addstr(&buf, "From: ");
			strbuf_add(&buf, namebuf, namelen);
			strbuf_addstr(&buf, " <");
			strbuf_add(&buf, mailbuf, maillen);
			strbuf_addstr(&buf, ">\n");
			string_list_append(&pp->in_body_headers,
					   strbuf_detach(&buf, NULL));

			mailbuf = pp->from_ident->mail_begin;
			maillen = pp->from_ident->mail_end - mailbuf;
			namebuf = pp->from_ident->name_begin;
			namelen = pp->from_ident->name_end - namebuf;
		}

		strbuf_addstr(sb, "From: ");
		if (pp->encode_email_headers &&
		    needs_rfc2047_encoding(namebuf, namelen)) {
			add_rfc2047(sb, namebuf, namelen,
				    encoding, RFC2047_ADDRESS);
			max_length = 76; /* per rfc2047 */
		} else if (needs_rfc822_quoting(namebuf, namelen)) {
			struct strbuf quoted = STRBUF_INIT;
			add_rfc822_quoted(&quoted, namebuf, namelen);
			strbuf_add_wrapped_bytes(sb, quoted.buf, quoted.len,
							-6, 1, max_length);
			strbuf_release(&quoted);
		} else {
			strbuf_add_wrapped_bytes(sb, namebuf, namelen,
						 -6, 1, max_length);
		}

		if (max_length <
		    last_line_length(sb) + strlen(" <") + maillen + strlen(">"))
			strbuf_addch(sb, '\n');
		strbuf_addf(sb, " <%.*s>\n", (int)maillen, mailbuf);
	} else {
		struct strbuf id = STRBUF_INIT;
		enum grep_header_field field = GREP_HEADER_FIELD_MAX;
		struct grep_opt *opt = pp->rev ? &pp->rev->grep_filter : NULL;

		if (!strcmp(what, "Author"))
			field = GREP_HEADER_AUTHOR;
		else if (!strcmp(what, "Commit"))
			field = GREP_HEADER_COMMITTER;

		strbuf_addf(sb, "%s: ", what);
		if (pp->fmt == CMIT_FMT_FULLER)
			strbuf_addchars(sb, ' ', 4);

		strbuf_addf(&id, "%.*s <%.*s>", (int)namelen, namebuf,
			    (int)maillen, mailbuf);

		append_line_with_color(sb, opt, id.buf, id.len, pp->color,
				       GREP_CONTEXT_HEAD, field);
		strbuf_addch(sb, '\n');
		strbuf_release(&id);
	}

	switch (pp->fmt) {
	case CMIT_FMT_MEDIUM:
		strbuf_addf(sb, "Date:   %s\n",
			    show_ident_date(&ident, pp->date_mode));
		break;
	case CMIT_FMT_EMAIL:
	case CMIT_FMT_MBOXRD:
		strbuf_addf(sb, "Date: %s\n",
			    show_ident_date(&ident, DATE_MODE(RFC2822)));
		break;
	case CMIT_FMT_FULLER:
		strbuf_addf(sb, "%sDate: %s\n", what,
			    show_ident_date(&ident, pp->date_mode));
		break;
	default:
		/* notin' */
		break;
	}
}

static int is_blank_line(const char *line, int *len_p)
{
	int len = *len_p;
	while (len && isspace(line[len - 1]))
		len--;
	*len_p = len;
	return !len;
}

const char *skip_blank_lines(const char *msg)
{
	for (;;) {
		int linelen = get_one_line(msg);
		int ll = linelen;
		if (!linelen)
			break;
		if (!is_blank_line(msg, &ll))
			break;
		msg += linelen;
	}
	return msg;
}

static void add_merge_info(const struct pretty_print_context *pp,
			   struct strbuf *sb, const struct commit *commit)
{
	struct commit_list *parent = commit->parents;

	if ((pp->fmt == CMIT_FMT_ONELINE) || (cmit_fmt_is_mail(pp->fmt)) ||
	    !parent || !parent->next)
		return;

	strbuf_addstr(sb, "Merge:");

	while (parent) {
		struct object_id *oidp = &parent->item->object.oid;
		strbuf_addch(sb, ' ');
		if (pp->abbrev)
			strbuf_add_unique_abbrev(sb, oidp, pp->abbrev);
		else
			strbuf_addstr(sb, oid_to_hex(oidp));
		parent = parent->next;
	}
	strbuf_addch(sb, '\n');
}

static char *get_header(const char *msg, const char *key)
{
	size_t len;
	const char *v = find_commit_header(msg, key, &len);
	return v ? xmemdupz(v, len) : NULL;
}

static char *replace_encoding_header(char *buf, const char *encoding)
{
	struct strbuf tmp = STRBUF_INIT;
	size_t start, len;
	char *cp = buf;

	/* guess if there is an encoding header before a \n\n */
	while (!starts_with(cp, "encoding ")) {
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

const char *repo_logmsg_reencode(struct repository *r,
				 const struct commit *commit,
				 char **commit_encoding,
				 const char *output_encoding)
{
	static const char *utf8 = "UTF-8";
	const char *use_encoding;
	char *encoding;
	const char *msg = repo_get_commit_buffer(r, commit, NULL);
	char *out;

	if (!output_encoding || !*output_encoding) {
		if (commit_encoding)
			*commit_encoding = get_header(msg, "encoding");
		return msg;
	}
	encoding = get_header(msg, "encoding");
	if (commit_encoding)
		*commit_encoding = encoding;
	use_encoding = encoding ? encoding : utf8;
	if (same_encoding(use_encoding, output_encoding)) {
		/*
		 * No encoding work to be done. If we have no encoding header
		 * at all, then there's nothing to do, and we can return the
		 * message verbatim (whether newly allocated or not).
		 */
		if (!encoding)
			return msg;

		/*
		 * Otherwise, we still want to munge the encoding header in the
		 * result, which will be done by modifying the buffer. If we
		 * are using a fresh copy, we can reuse it. But if we are using
		 * the cached copy from repo_get_commit_buffer, we need to duplicate it
		 * to avoid munging the cached copy.
		 */
		if (msg == get_cached_commit_buffer(r, commit, NULL))
			out = xstrdup(msg);
		else
			out = (char *)msg;
	}
	else {
		/*
		 * There's actual encoding work to do. Do the reencoding, which
		 * still leaves the header to be replaced in the next step. At
		 * this point, we are done with msg. If we allocated a fresh
		 * copy, we can free it.
		 */
		out = reencode_string(msg, output_encoding, use_encoding);
		if (out)
			repo_unuse_commit_buffer(r, commit, msg);
	}

	/*
	 * This replacement actually consumes the buffer we hand it, so we do
	 * not have to worry about freeing the old "out" here.
	 */
	if (out)
		out = replace_encoding_header(out, output_encoding);

	if (!commit_encoding)
		free(encoding);
	/*
	 * If the re-encoding failed, out might be NULL here; in that
	 * case we just return the commit message verbatim.
	 */
	return out ? out : msg;
}

static int mailmap_name(const char **email, size_t *email_len,
			const char **name, size_t *name_len)
{
	static struct string_list *mail_map;
	if (!mail_map) {
		CALLOC_ARRAY(mail_map, 1);
		read_mailmap(mail_map);
	}
	return mail_map->nr && map_user(mail_map, email, email_len, name, name_len);
}

static size_t format_person_part(struct strbuf *sb, char part,
				 const char *msg, int len,
				 struct date_mode dmode)
{
	/* currently all placeholders have same length */
	const int placeholder_len = 2;
	struct ident_split s;
	const char *name, *mail;
	size_t maillen, namelen;

	if (split_ident_line(&s, msg, len) < 0)
		goto skip;

	name = s.name_begin;
	namelen = s.name_end - s.name_begin;
	mail = s.mail_begin;
	maillen = s.mail_end - s.mail_begin;

	if (part == 'N' || part == 'E' || part == 'L') /* mailmap lookup */
		mailmap_name(&mail, &maillen, &name, &namelen);
	if (part == 'n' || part == 'N') {	/* name */
		strbuf_add(sb, name, namelen);
		return placeholder_len;
	}
	if (part == 'e' || part == 'E') {	/* email */
		strbuf_add(sb, mail, maillen);
		return placeholder_len;
	}
	if (part == 'l' || part == 'L') {	/* local-part */
		const char *at = memchr(mail, '@', maillen);
		if (at)
			maillen = at - mail;
		strbuf_add(sb, mail, maillen);
		return placeholder_len;
	}

	if (!s.date_begin)
		goto skip;

	if (part == 't') {	/* date, UNIX timestamp */
		strbuf_add(sb, s.date_begin, s.date_end - s.date_begin);
		return placeholder_len;
	}

	switch (part) {
	case 'd':	/* date */
		strbuf_addstr(sb, show_ident_date(&s, dmode));
		return placeholder_len;
	case 'D':	/* date, RFC2822 style */
		strbuf_addstr(sb, show_ident_date(&s, DATE_MODE(RFC2822)));
		return placeholder_len;
	case 'r':	/* date, relative */
		strbuf_addstr(sb, show_ident_date(&s, DATE_MODE(RELATIVE)));
		return placeholder_len;
	case 'i':	/* date, ISO 8601-like */
		strbuf_addstr(sb, show_ident_date(&s, DATE_MODE(ISO8601)));
		return placeholder_len;
	case 'I':	/* date, ISO 8601 strict */
		strbuf_addstr(sb, show_ident_date(&s, DATE_MODE(ISO8601_STRICT)));
		return placeholder_len;
	case 'h':	/* date, human */
		strbuf_addstr(sb, show_ident_date(&s, DATE_MODE(HUMAN)));
		return placeholder_len;
	case 's':
		strbuf_addstr(sb, show_ident_date(&s, DATE_MODE(SHORT)));
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

enum flush_type {
	no_flush,
	flush_right,
	flush_left,
	flush_left_and_steal,
	flush_both
};

enum trunc_type {
	trunc_none,
	trunc_left,
	trunc_middle,
	trunc_right
};

struct format_commit_context {
	struct repository *repository;
	const struct commit *commit;
	const struct pretty_print_context *pretty_ctx;
	unsigned commit_header_parsed:1;
	unsigned commit_message_parsed:1;
	struct signature_check signature_check;
	enum flush_type flush_type;
	enum trunc_type truncate;
	const char *message;
	char *commit_encoding;
	size_t width, indent1, indent2;
	int auto_color;
	int padding;

	/* These offsets are relative to the start of the commit message. */
	struct chunk author;
	struct chunk committer;
	size_t message_off;
	size_t subject_off;
	size_t body_off;

	/* The following ones are relative to the result struct strbuf. */
	size_t wrap_start;
};

static void parse_commit_header(struct format_commit_context *context)
{
	const char *msg = context->message;
	int i;

	for (i = 0; msg[i]; i++) {
		const char *name;
		int eol;
		for (eol = i; msg[eol] && msg[eol] != '\n'; eol++)
			; /* do nothing */

		if (i == eol) {
			break;
		} else if (skip_prefix(msg + i, "author ", &name)) {
			context->author.off = name - msg;
			context->author.len = msg + eol - name;
		} else if (skip_prefix(msg + i, "committer ", &name)) {
			context->committer.off = name - msg;
			context->committer.len = msg + eol - name;
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

void format_sanitized_subject(struct strbuf *sb, const char *msg, size_t len)
{
	size_t trimlen;
	size_t start_len = sb->len;
	int space = 2;
	int i;

	for (i = 0; i < len; i++) {
		if (istitlechar(msg[i])) {
			if (space == 1)
				strbuf_addch(sb, '-');
			space = 0;
			strbuf_addch(sb, msg[i]);
			if (msg[i] == '.')
				while (msg[i+1] == '.')
					i++;
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
		if (!linelen || is_blank_line(line, &linelen))
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

	msg = skip_blank_lines(msg);
	c->subject_off = msg - start;

	msg = format_subject(NULL, msg, NULL);
	msg = skip_blank_lines(msg);
	c->body_off = msg - start;

	c->commit_message_parsed = 1;
}

static void strbuf_wrap(struct strbuf *sb, size_t pos,
			size_t width, size_t indent1, size_t indent2)
{
	struct strbuf tmp = STRBUF_INIT;

	if (pos)
		strbuf_add(&tmp, sb->buf, pos);
	strbuf_add_wrapped_text(&tmp, sb->buf + pos,
				cast_size_t_to_int(indent1),
				cast_size_t_to_int(indent2),
				cast_size_t_to_int(width));
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

static int format_reflog_person(struct strbuf *sb,
				char part,
				struct reflog_walk_info *log,
				struct date_mode dmode)
{
	const char *ident;

	if (!log)
		return 2;

	ident = get_reflog_ident(log);
	if (!ident)
		return 2;

	return format_person_part(sb, part, ident, strlen(ident), dmode);
}

static size_t parse_color(struct strbuf *sb, /* in UTF-8 */
			  const char *placeholder,
			  struct format_commit_context *c)
{
	const char *rest = placeholder;
	const char *basic_color = NULL;

	if (placeholder[1] == '(') {
		const char *begin = placeholder + 2;
		const char *end = strchr(begin, ')');
		char color[COLOR_MAXLEN];

		if (!end)
			return 0;

		if (skip_prefix(begin, "auto,", &begin)) {
			if (!want_color(c->pretty_ctx->color))
				return end - placeholder + 1;
		} else if (skip_prefix(begin, "always,", &begin)) {
			/* nothing to do; we do not respect want_color at all */
		} else {
			/* the default is the same as "auto" */
			if (!want_color(c->pretty_ctx->color))
				return end - placeholder + 1;
		}

		if (color_parse_mem(begin, end - begin, color) < 0)
			die(_("unable to parse --pretty format"));
		strbuf_addstr(sb, color);
		return end - placeholder + 1;
	}

	/*
	 * We handle things like "%C(red)" above; for historical reasons, there
	 * are a few colors that can be specified without parentheses (and
	 * they cannot support things like "auto" or "always" at all).
	 */
	if (skip_prefix(placeholder + 1, "red", &rest))
		basic_color = GIT_COLOR_RED;
	else if (skip_prefix(placeholder + 1, "green", &rest))
		basic_color = GIT_COLOR_GREEN;
	else if (skip_prefix(placeholder + 1, "blue", &rest))
		basic_color = GIT_COLOR_BLUE;
	else if (skip_prefix(placeholder + 1, "reset", &rest))
		basic_color = GIT_COLOR_RESET;

	if (basic_color && want_color(c->pretty_ctx->color))
		strbuf_addstr(sb, basic_color);

	return rest - placeholder;
}

static size_t parse_padding_placeholder(const char *placeholder,
					struct format_commit_context *c)
{
	const char *ch = placeholder;
	enum flush_type flush_type;
	int to_column = 0;

	switch (*ch++) {
	case '<':
		flush_type = flush_right;
		break;
	case '>':
		if (*ch == '<') {
			flush_type = flush_both;
			ch++;
		} else if (*ch == '>') {
			flush_type = flush_left_and_steal;
			ch++;
		} else
			flush_type = flush_left;
		break;
	default:
		return 0;
	}

	/* the next value means "wide enough to that column" */
	if (*ch == '|') {
		to_column = 1;
		ch++;
	}

	if (*ch == '(') {
		const char *start = ch + 1;
		const char *end = start + strcspn(start, ",)");
		char *next;
		int width;
		if (!*end || end == start)
			return 0;
		width = strtol(start, &next, 10);

		/*
		 * We need to limit the amount of padding, or otherwise this
		 * would allow the user to pad the buffer by arbitrarily many
		 * bytes and thus cause resource exhaustion.
		 */
		if (width < -FORMATTING_LIMIT || width > FORMATTING_LIMIT)
			return 0;

		if (next == start || width == 0)
			return 0;
		if (width < 0) {
			if (to_column)
				width += term_columns();
			if (width < 0)
				return 0;
		}
		c->padding = to_column ? -width : width;
		c->flush_type = flush_type;

		if (*end == ',') {
			start = end + 1;
			end = strchr(start, ')');
			if (!end || end == start)
				return 0;
			if (starts_with(start, "trunc)"))
				c->truncate = trunc_right;
			else if (starts_with(start, "ltrunc)"))
				c->truncate = trunc_left;
			else if (starts_with(start, "mtrunc)"))
				c->truncate = trunc_middle;
			else
				return 0;
		} else
			c->truncate = trunc_none;

		return end - placeholder + 1;
	}
	return 0;
}

static int match_placeholder_arg_value(const char *to_parse, const char *candidate,
				       const char **end, const char **valuestart,
				       size_t *valuelen)
{
	const char *p;

	if (!(skip_prefix(to_parse, candidate, &p)))
		return 0;
	if (valuestart) {
		if (*p == '=') {
			*valuestart = p + 1;
			*valuelen = strcspn(*valuestart, ",)");
			p = *valuestart + *valuelen;
		} else {
			if (*p != ',' && *p != ')')
				return 0;
			*valuestart = NULL;
			*valuelen = 0;
		}
	}
	if (*p == ',') {
		*end = p + 1;
		return 1;
	}
	if (*p == ')') {
		*end = p;
		return 1;
	}
	return 0;
}

static int match_placeholder_bool_arg(const char *to_parse, const char *candidate,
				      const char **end, int *val)
{
	const char *argval;
	char *strval;
	size_t arglen;
	int v;

	if (!match_placeholder_arg_value(to_parse, candidate, end, &argval, &arglen))
		return 0;

	if (!argval) {
		*val = 1;
		return 1;
	}

	strval = xstrndup(argval, arglen);
	v = git_parse_maybe_bool(strval);
	free(strval);

	if (v == -1)
		return 0;

	*val = v;

	return 1;
}

static int format_trailer_match_cb(const struct strbuf *key, void *ud)
{
	const struct string_list *list = ud;
	const struct string_list_item *item;

	for_each_string_list_item (item, list) {
		if (key->len == (uintptr_t)item->util &&
		    !strncasecmp(item->string, key->buf, key->len))
			return 1;
	}
	return 0;
}

static struct strbuf *expand_string_arg(struct strbuf *sb,
					const char *argval, size_t arglen)
{
	char *fmt = xstrndup(argval, arglen);
	const char *format = fmt;

	strbuf_reset(sb);
	while (strbuf_expand_step(sb, &format)) {
		size_t len;

		if (skip_prefix(format, "%", &format))
			strbuf_addch(sb, '%');
		else if ((len = strbuf_expand_literal(sb, format)))
			format += len;
		else
			strbuf_addch(sb, '%');
	}
	free(fmt);
	return sb;
}

int format_set_trailers_options(struct process_trailer_options *opts,
				struct string_list *filter_list,
				struct strbuf *sepbuf,
				struct strbuf *kvsepbuf,
				const char **arg,
				char **invalid_arg)
{
	for (;;) {
		const char *argval;
		size_t arglen;

		if (**arg == ')')
			break;

		if (match_placeholder_arg_value(*arg, "key", arg, &argval, &arglen)) {
			uintptr_t len = arglen;

			if (!argval)
				return -1;

			if (len && argval[len - 1] == ':')
				len--;
			string_list_append(filter_list, argval)->util = (char *)len;

			opts->filter = format_trailer_match_cb;
			opts->filter_data = filter_list;
			opts->only_trailers = 1;
		} else if (match_placeholder_arg_value(*arg, "separator", arg, &argval, &arglen)) {
			opts->separator = expand_string_arg(sepbuf, argval, arglen);
		} else if (match_placeholder_arg_value(*arg, "key_value_separator", arg, &argval, &arglen)) {
			opts->key_value_separator = expand_string_arg(kvsepbuf, argval, arglen);
		} else if (!match_placeholder_bool_arg(*arg, "only", arg, &opts->only_trailers) &&
			   !match_placeholder_bool_arg(*arg, "unfold", arg, &opts->unfold) &&
			   !match_placeholder_bool_arg(*arg, "keyonly", arg, &opts->key_only) &&
			   !match_placeholder_bool_arg(*arg, "valueonly", arg, &opts->value_only)) {
			if (invalid_arg) {
				size_t len = strcspn(*arg, ",)");
				*invalid_arg = xstrndup(*arg, len);
			}
			return -1;
		}
	}
	return 0;
}

static size_t parse_describe_args(const char *start, struct strvec *args)
{
	struct {
		const char *name;
		enum {
			DESCRIBE_ARG_BOOL,
			DESCRIBE_ARG_INTEGER,
			DESCRIBE_ARG_STRING,
		} type;
	}  option[] = {
		{ "tags", DESCRIBE_ARG_BOOL},
		{ "abbrev", DESCRIBE_ARG_INTEGER },
		{ "exclude", DESCRIBE_ARG_STRING },
		{ "match", DESCRIBE_ARG_STRING },
	};
	const char *arg = start;

	for (;;) {
		int found = 0;
		const char *argval;
		size_t arglen = 0;
		int optval = 0;
		int i;

		for (i = 0; !found && i < ARRAY_SIZE(option); i++) {
			switch (option[i].type) {
			case DESCRIBE_ARG_BOOL:
				if (match_placeholder_bool_arg(arg, option[i].name, &arg, &optval)) {
					if (optval)
						strvec_pushf(args, "--%s", option[i].name);
					else
						strvec_pushf(args, "--no-%s", option[i].name);
					found = 1;
				}
				break;
			case DESCRIBE_ARG_INTEGER:
				if (match_placeholder_arg_value(arg, option[i].name, &arg,
								&argval, &arglen)) {
					char *endptr;
					if (!arglen)
						return 0;
					strtol(argval, &endptr, 10);
					if (endptr - argval != arglen)
						return 0;
					strvec_pushf(args, "--%s=%.*s", option[i].name, (int)arglen, argval);
					found = 1;
				}
				break;
			case DESCRIBE_ARG_STRING:
				if (match_placeholder_arg_value(arg, option[i].name, &arg,
								&argval, &arglen)) {
					if (!arglen)
						return 0;
					strvec_pushf(args, "--%s=%.*s", option[i].name, (int)arglen, argval);
					found = 1;
				}
				break;
			}
		}
		if (!found)
			break;

	}
	return arg - start;
}


static int parse_decoration_option(const char **arg,
				   const char *name,
				   char **opt)
{
	const char *argval;
	size_t arglen;

	if (match_placeholder_arg_value(*arg, name, arg, &argval, &arglen)) {
		struct strbuf sb = STRBUF_INIT;

		expand_string_arg(&sb, argval, arglen);
		*opt = strbuf_detach(&sb, NULL);
		return 1;
	}
	return 0;
}

static void parse_decoration_options(const char **arg,
				     struct decoration_options *opts)
{
	while (parse_decoration_option(arg, "prefix", &opts->prefix) ||
	       parse_decoration_option(arg, "suffix", &opts->suffix) ||
	       parse_decoration_option(arg, "separator", &opts->separator) ||
	       parse_decoration_option(arg, "pointer", &opts->pointer) ||
	       parse_decoration_option(arg, "tag", &opts->tag))
		;
}

static void free_decoration_options(const struct decoration_options *opts)
{
	free(opts->prefix);
	free(opts->suffix);
	free(opts->separator);
	free(opts->pointer);
	free(opts->tag);
}

static size_t format_commit_one(struct strbuf *sb, /* in UTF-8 */
				const char *placeholder,
				void *context)
{
	struct format_commit_context *c = context;
	const struct commit *commit = c->commit;
	const char *msg = c->message;
	struct commit_list *p;
	const char *arg, *eol;
	size_t res;
	char **slot;

	/* these are independent of the commit */
	res = strbuf_expand_literal(sb, placeholder);
	if (res)
		return res;

	switch (placeholder[0]) {
	case 'C':
		if (starts_with(placeholder + 1, "(auto)")) {
			c->auto_color = want_color(c->pretty_ctx->color);
			if (c->auto_color && sb->len)
				strbuf_addstr(sb, GIT_COLOR_RESET);
			return 7; /* consumed 7 bytes, "C(auto)" */
		} else {
			int ret = parse_color(sb, placeholder, c);
			if (ret)
				c->auto_color = 0;
			/*
			 * Otherwise, we decided to treat %C<unknown>
			 * as a literal string, and the previous
			 * %C(auto) is still valid.
			 */
			return ret;
		}
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

			/*
			 * We need to limit the format here as it allows the
			 * user to prepend arbitrarily many bytes to the buffer
			 * when rewrapping.
			 */
			if (width > FORMATTING_LIMIT ||
			    indent1 > FORMATTING_LIMIT ||
			    indent2 > FORMATTING_LIMIT)
				return 0;
			rewrap_message_tail(sb, c, width, indent1, indent2);
			return end - placeholder + 1;
		} else
			return 0;

	case '<':
	case '>':
		return parse_padding_placeholder(placeholder, c);
	}

	if (skip_prefix(placeholder, "(describe", &arg)) {
		struct child_process cmd = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;
		struct strbuf err = STRBUF_INIT;
		struct pretty_print_describe_status *describe_status;

		describe_status = c->pretty_ctx->describe_status;
		if (describe_status) {
			if (!describe_status->max_invocations)
				return 0;
			describe_status->max_invocations--;
		}

		cmd.git_cmd = 1;
		strvec_push(&cmd.args, "describe");

		if (*arg == ':') {
			arg++;
			arg += parse_describe_args(arg, &cmd.args);
		}

		if (*arg != ')') {
			child_process_clear(&cmd);
			return 0;
		}

		strvec_push(&cmd.args, oid_to_hex(&commit->object.oid));
		pipe_command(&cmd, NULL, 0, &out, 0, &err, 0);
		strbuf_rtrim(&out);
		strbuf_addbuf(sb, &out);
		strbuf_release(&out);
		strbuf_release(&err);
		return arg - placeholder + 1;
	}

	/* these depend on the commit */
	if (!commit->object.parsed)
		parse_object(the_repository, &commit->object.oid);

	switch (placeholder[0]) {
	case 'H':		/* commit hash */
		strbuf_addstr(sb, diff_get_color(c->auto_color, DIFF_COMMIT));
		strbuf_addstr(sb, oid_to_hex(&commit->object.oid));
		strbuf_addstr(sb, diff_get_color(c->auto_color, DIFF_RESET));
		return 1;
	case 'h':		/* abbreviated commit hash */
		strbuf_addstr(sb, diff_get_color(c->auto_color, DIFF_COMMIT));
		strbuf_add_unique_abbrev(sb, &commit->object.oid,
					 c->pretty_ctx->abbrev);
		strbuf_addstr(sb, diff_get_color(c->auto_color, DIFF_RESET));
		return 1;
	case 'T':		/* tree hash */
		strbuf_addstr(sb, oid_to_hex(get_commit_tree_oid(commit)));
		return 1;
	case 't':		/* abbreviated tree hash */
		strbuf_add_unique_abbrev(sb,
					 get_commit_tree_oid(commit),
					 c->pretty_ctx->abbrev);
		return 1;
	case 'P':		/* parent hashes */
		for (p = commit->parents; p; p = p->next) {
			if (p != commit->parents)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, oid_to_hex(&p->item->object.oid));
		}
		return 1;
	case 'p':		/* abbreviated parent hashes */
		for (p = commit->parents; p; p = p->next) {
			if (p != commit->parents)
				strbuf_addch(sb, ' ');
			strbuf_add_unique_abbrev(sb, &p->item->object.oid,
						 c->pretty_ctx->abbrev);
		}
		return 1;
	case 'm':		/* left/right/bottom */
		strbuf_addstr(sb, get_revision_mark(NULL, commit));
		return 1;
	case 'd':
		format_decorations(sb, commit, c->auto_color, NULL);
		return 1;
	case 'D':
		{
			const struct decoration_options opts = {
				.prefix = (char *) "",
				.suffix = (char *) "",
			};

			format_decorations(sb, commit, c->auto_color, &opts);
			return 1;
		}
	case 'S':		/* tag/branch like --source */
		if (!(c->pretty_ctx->rev && c->pretty_ctx->rev->sources))
			return 0;
		slot = revision_sources_at(c->pretty_ctx->rev->sources, commit);
		if (!(slot && *slot))
			return 0;
		strbuf_addstr(sb, *slot);
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
		if (c->pretty_ctx->notes_message) {
			strbuf_addstr(sb, c->pretty_ctx->notes_message);
			return 1;
		}
		return 0;
	}

	if (placeholder[0] == 'G') {
		if (!c->signature_check.result)
			check_commit_signature(c->commit, &(c->signature_check));
		switch (placeholder[1]) {
		case 'G':
			if (c->signature_check.output)
				strbuf_addstr(sb, c->signature_check.output);
			break;
		case '?':
			switch (c->signature_check.result) {
			case 'G':
				switch (c->signature_check.trust_level) {
				case TRUST_UNDEFINED:
				case TRUST_NEVER:
					strbuf_addch(sb, 'U');
					break;
				default:
					strbuf_addch(sb, 'G');
					break;
				}
				break;
			case 'B':
			case 'E':
			case 'N':
			case 'X':
			case 'Y':
			case 'R':
				strbuf_addch(sb, c->signature_check.result);
			}
			break;
		case 'S':
			if (c->signature_check.signer)
				strbuf_addstr(sb, c->signature_check.signer);
			break;
		case 'K':
			if (c->signature_check.key)
				strbuf_addstr(sb, c->signature_check.key);
			break;
		case 'F':
			if (c->signature_check.fingerprint)
				strbuf_addstr(sb, c->signature_check.fingerprint);
			break;
		case 'P':
			if (c->signature_check.primary_key_fingerprint)
				strbuf_addstr(sb, c->signature_check.primary_key_fingerprint);
			break;
		case 'T':
			strbuf_addstr(sb, gpg_trust_level_to_str(c->signature_check.trust_level));
			break;
		default:
			return 0;
		}
		return 2;
	}

	if (skip_prefix(placeholder, "(decorate", &arg)) {
		struct decoration_options opts = { NULL };
		size_t ret = 0;

		if (*arg == ':') {
			arg++;
			parse_decoration_options(&arg, &opts);
		}
		if (*arg == ')') {
			format_decorations(sb, commit, c->auto_color, &opts);
			ret = arg - placeholder + 1;
		}

		free_decoration_options(&opts);
		return ret;
	}

	/* For the rest we have to parse the commit header. */
	if (!c->commit_header_parsed) {
		msg = c->message =
			repo_logmsg_reencode(c->repository, commit,
					     &c->commit_encoding, "UTF-8");
		parse_commit_header(c);
	}

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
		if (c->commit_encoding)
			strbuf_addstr(sb, c->commit_encoding);
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
		eol = strchrnul(msg + c->subject_off, '\n');
		format_sanitized_subject(sb, msg + c->subject_off, eol - (msg + c->subject_off));
		return 1;
	case 'b':	/* body */
		strbuf_addstr(sb, msg + c->body_off);
		return 1;
	}

	if (skip_prefix(placeholder, "(trailers", &arg)) {
		struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
		struct string_list filter_list = STRING_LIST_INIT_NODUP;
		struct strbuf sepbuf = STRBUF_INIT;
		struct strbuf kvsepbuf = STRBUF_INIT;
		size_t ret = 0;

		opts.no_divider = 1;

		if (*arg == ':') {
			arg++;
			if (format_set_trailers_options(&opts, &filter_list, &sepbuf, &kvsepbuf, &arg, NULL))
				goto trailer_out;
		}
		if (*arg == ')') {
			format_trailers_from_commit(&opts, msg + c->subject_off, sb);
			ret = arg - placeholder + 1;
		}
	trailer_out:
		string_list_clear(&filter_list, 0);
		strbuf_release(&kvsepbuf);
		strbuf_release(&sepbuf);
		return ret;
	}

	return 0;	/* unknown placeholder */
}

static size_t format_and_pad_commit(struct strbuf *sb, /* in UTF-8 */
				    const char *placeholder,
				    struct format_commit_context *c)
{
	struct strbuf local_sb = STRBUF_INIT;
	size_t total_consumed = 0;
	int len, padding = c->padding;

	if (padding < 0) {
		const char *start = strrchr(sb->buf, '\n');
		int occupied;
		if (!start)
			start = sb->buf;
		occupied = utf8_strnwidth(start, strlen(start), 1);
		occupied += c->pretty_ctx->graph_width;
		padding = (-padding) - occupied;
	}
	while (1) {
		int modifier = *placeholder == 'C';
		size_t consumed = format_commit_one(&local_sb, placeholder, c);
		total_consumed += consumed;

		if (!modifier)
			break;

		placeholder += consumed;
		if (*placeholder != '%')
			break;
		placeholder++;
		total_consumed++;
	}
	len = utf8_strnwidth(local_sb.buf, local_sb.len, 1);

	if (c->flush_type == flush_left_and_steal) {
		const char *ch = sb->buf + sb->len - 1;
		while (len > padding && ch > sb->buf) {
			const char *p;
			if (*ch == ' ') {
				ch--;
				padding++;
				continue;
			}
			/* check for trailing ansi sequences */
			if (*ch != 'm')
				break;
			p = ch - 1;
			while (p > sb->buf && ch - p < 10 && *p != '\033')
				p--;
			if (*p != '\033' ||
			    ch + 1 - p != display_mode_esc_sequence_len(p))
				break;
			/*
			 * got a good ansi sequence, put it back to
			 * local_sb as we're cutting sb
			 */
			strbuf_insert(&local_sb, 0, p, ch + 1 - p);
			ch = p - 1;
		}
		strbuf_setlen(sb, ch + 1 - sb->buf);
		c->flush_type = flush_left;
	}

	if (len > padding) {
		switch (c->truncate) {
		case trunc_left:
			strbuf_utf8_replace(&local_sb,
					    0, len - (padding - 2),
					    "..");
			break;
		case trunc_middle:
			strbuf_utf8_replace(&local_sb,
					    padding / 2 - 1,
					    len - (padding - 2),
					    "..");
			break;
		case trunc_right:
			strbuf_utf8_replace(&local_sb,
					    padding - 2, len - (padding - 2),
					    "..");
			break;
		case trunc_none:
			break;
		}
		strbuf_addbuf(sb, &local_sb);
	} else {
		size_t sb_len = sb->len, offset = 0;
		if (c->flush_type == flush_left)
			offset = padding - len;
		else if (c->flush_type == flush_both)
			offset = (padding - len) / 2;
		/*
		 * we calculate padding in columns, now
		 * convert it back to chars
		 */
		padding = padding - len + local_sb.len;
		strbuf_addchars(sb, ' ', padding);
		memcpy(sb->buf + sb_len + offset, local_sb.buf,
		       local_sb.len);
	}
	strbuf_release(&local_sb);
	c->flush_type = no_flush;
	return total_consumed;
}

static size_t format_commit_item(struct strbuf *sb, /* in UTF-8 */
				 const char *placeholder,
				 struct format_commit_context *context)
{
	size_t consumed, orig_len;
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
	if (magic != NO_MAGIC) {
		placeholder++;

		switch (placeholder[0]) {
		case 'w':
			/*
			 * `%+w()` cannot ever expand to a non-empty string,
			 * and it potentially changes the layout of preceding
			 * contents. We're thus not able to handle the magic in
			 * this combination and refuse the pattern.
			 */
			return 0;
		};
	}

	orig_len = sb->len;
	if (context->flush_type == no_flush)
		consumed = format_commit_one(sb, placeholder, context);
	else
		consumed = format_and_pad_commit(sb, placeholder, context);
	if (magic == NO_MAGIC)
		return consumed;

	if ((orig_len == sb->len) && magic == DEL_LF_BEFORE_EMPTY) {
		while (sb->len && sb->buf[sb->len - 1] == '\n')
			strbuf_setlen(sb, sb->len - 1);
	} else if (orig_len != sb->len) {
		if (magic == ADD_LF_BEFORE_NON_EMPTY)
			strbuf_insertstr(sb, orig_len, "\n");
		else if (magic == ADD_SP_BEFORE_NON_EMPTY)
			strbuf_insertstr(sb, orig_len, " ");
	}
	return consumed + 1;
}

void userformat_find_requirements(const char *fmt, struct userformat_want *w)
{
	if (!fmt) {
		if (!user_format)
			return;
		fmt = user_format;
	}
	while ((fmt = strchr(fmt, '%'))) {
		fmt++;
		if (skip_prefix(fmt, "%", &fmt))
			continue;

		if (*fmt == '+' || *fmt == '-' || *fmt == ' ')
			fmt++;

		switch (*fmt) {
		case 'N':
			w->notes = 1;
			break;
		case 'S':
			w->source = 1;
			break;
		case 'd':
		case 'D':
			w->decorate = 1;
			break;
		case '(':
			if (starts_with(fmt + 1, "decorate"))
				w->decorate = 1;
			break;
		}
	}
}

void repo_format_commit_message(struct repository *r,
				const struct commit *commit,
				const char *format, struct strbuf *sb,
				const struct pretty_print_context *pretty_ctx)
{
	struct format_commit_context context = {
		.repository = r,
		.commit = commit,
		.pretty_ctx = pretty_ctx,
		.wrap_start = sb->len
	};
	const char *output_enc = pretty_ctx->output_encoding;
	const char *utf8 = "UTF-8";

	while (strbuf_expand_step(sb, &format)) {
		size_t len;

		if (skip_prefix(format, "%", &format))
			strbuf_addch(sb, '%');
		else if ((len = format_commit_item(sb, format, &context)))
			format += len;
		else
			strbuf_addch(sb, '%');
	}
	rewrap_message_tail(sb, &context, 0, 0, 0);

	/*
	 * Convert output to an actual output encoding; note that
	 * format_commit_item() will always use UTF-8, so we don't
	 * have to bother if that's what the output wants.
	 */
	if (output_enc) {
		if (same_encoding(utf8, output_enc))
			output_enc = NULL;
	} else {
		if (context.commit_encoding &&
		    !same_encoding(context.commit_encoding, utf8))
			output_enc = context.commit_encoding;
	}

	if (output_enc) {
		size_t outsz;
		char *out = reencode_string_len(sb->buf, sb->len,
						output_enc, utf8, &outsz);
		if (out)
			strbuf_attach(sb, out, outsz, outsz + 1);
	}

	free(context.commit_encoding);
	repo_unuse_commit_buffer(r, commit, context.message);
	signature_check_clear(&context.signature_check);
}

static void pp_header(struct pretty_print_context *pp,
		      const char *encoding,
		      const struct commit *commit,
		      const char **msg_p,
		      struct strbuf *sb)
{
	int parents_shown = 0;

	for (;;) {
		const char *name, *line = *msg_p;
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

		if (starts_with(line, "parent ")) {
			if (linelen != the_hash_algo->hexsz + 8)
				die("bad parent line in commit");
			continue;
		}

		if (!parents_shown) {
			unsigned num = commit_list_count(commit->parents);
			/* with enough slop */
			strbuf_grow(sb, num * (GIT_MAX_HEXSZ + 10) + 20);
			add_merge_info(pp, sb, commit);
			parents_shown = 1;
		}

		/*
		 * MEDIUM == DEFAULT shows only author with dates.
		 * FULL shows both authors but not dates.
		 * FULLER shows both authors and dates.
		 */
		if (skip_prefix(line, "author ", &name)) {
			strbuf_grow(sb, linelen + 80);
			pp_user_info(pp, "Author", sb, name, encoding);
		}
		if (skip_prefix(line, "committer ", &name) &&
		    (pp->fmt == CMIT_FMT_FULL || pp->fmt == CMIT_FMT_FULLER)) {
			strbuf_grow(sb, linelen + 80);
			pp_user_info(pp, "Commit", sb, name, encoding);
		}
	}
}

void pp_email_subject(struct pretty_print_context *pp,
		      const char **msg_p,
		      struct strbuf *sb,
		      const char *encoding,
		      int need_8bit_cte)
{
	static const int max_length = 78; /* per rfc2047 */
	struct strbuf title;

	strbuf_init(&title, 80);
	*msg_p = format_subject(&title, *msg_p,
				pp->preserve_subject ? "\n" : " ");

	strbuf_grow(sb, title.len + 1024);
	fmt_output_email_subject(sb, pp->rev);
	if (pp->encode_email_headers &&
	    needs_rfc2047_encoding(title.buf, title.len))
		add_rfc2047(sb, title.buf, title.len,
			    encoding, RFC2047_SUBJECT);
	else
		strbuf_add_wrapped_bytes(sb, title.buf, title.len,
					 -last_line_length(sb), 1, max_length);
	strbuf_addch(sb, '\n');

	if (need_8bit_cte == 0) {
		int i;
		for (i = 0; i < pp->in_body_headers.nr; i++) {
			if (has_non_ascii(pp->in_body_headers.items[i].string)) {
				need_8bit_cte = 1;
				break;
			}
		}
	}

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

	strbuf_addch(sb, '\n');

	if (pp->in_body_headers.nr) {
		int i;
		for (i = 0; i < pp->in_body_headers.nr; i++) {
			strbuf_addstr(sb, pp->in_body_headers.items[i].string);
			free(pp->in_body_headers.items[i].string);
		}
		string_list_clear(&pp->in_body_headers, 0);
		strbuf_addch(sb, '\n');
	}

	strbuf_release(&title);
}

static int pp_utf8_width(const char *start, const char *end)
{
	int width = 0;
	size_t remain = end - start;

	while (remain) {
		int n = utf8_width(&start, &remain);
		if (n < 0 || !start)
			return -1;
		width += n;
	}
	return width;
}

static void strbuf_add_tabexpand(struct strbuf *sb, struct grep_opt *opt,
				 int color, int tabwidth, const char *line,
				 int linelen)
{
	const char *tab;

	while ((tab = memchr(line, '\t', linelen)) != NULL) {
		int width = pp_utf8_width(line, tab);

		/*
		 * If it wasn't well-formed utf8, or it
		 * had characters with badly defined
		 * width (control characters etc), just
		 * give up on trying to align things.
		 */
		if (width < 0)
			break;

		/* Output the data .. */
		append_line_with_color(sb, opt, line, tab - line, color,
				       GREP_CONTEXT_BODY,
				       GREP_HEADER_FIELD_MAX);

		/* .. and the de-tabified tab */
		strbuf_addchars(sb, ' ', tabwidth - (width % tabwidth));

		/* Skip over the printed part .. */
		linelen -= tab + 1 - line;
		line = tab + 1;
	}

	/*
	 * Print out everything after the last tab without
	 * worrying about width - there's nothing more to
	 * align.
	 */
	append_line_with_color(sb, opt, line, linelen, color, GREP_CONTEXT_BODY,
			       GREP_HEADER_FIELD_MAX);
}

/*
 * pp_handle_indent() prints out the indentation, and
 * the whole line (without the final newline), after
 * de-tabifying.
 */
static void pp_handle_indent(struct pretty_print_context *pp,
			     struct strbuf *sb, int indent,
			     const char *line, int linelen)
{
	struct grep_opt *opt = pp->rev ? &pp->rev->grep_filter : NULL;

	strbuf_addchars(sb, ' ', indent);
	if (pp->expand_tabs_in_log)
		strbuf_add_tabexpand(sb, opt, pp->color, pp->expand_tabs_in_log,
				     line, linelen);
	else
		append_line_with_color(sb, opt, line, linelen, pp->color,
				       GREP_CONTEXT_BODY,
				       GREP_HEADER_FIELD_MAX);
}

static int is_mboxrd_from(const char *line, int len)
{
	/*
	 * a line matching /^From $/ here would only have len == 4
	 * at this point because is_empty_line would've trimmed all
	 * trailing space
	 */
	return len > 4 && starts_with(line + strspn(line, ">"), "From ");
}

void pp_remainder(struct pretty_print_context *pp,
		  const char **msg_p,
		  struct strbuf *sb,
		  int indent)
{
	struct grep_opt *opt = pp->rev ? &pp->rev->grep_filter : NULL;
	int first = 1;

	for (;;) {
		const char *line = *msg_p;
		int linelen = get_one_line(line);
		*msg_p += linelen;

		if (!linelen)
			break;

		if (is_blank_line(line, &linelen)) {
			if (first)
				continue;
			if (pp->fmt == CMIT_FMT_SHORT)
				break;
		}
		first = 0;

		strbuf_grow(sb, linelen + indent + 20);
		if (indent)
			pp_handle_indent(pp, sb, indent, line, linelen);
		else if (pp->expand_tabs_in_log)
			strbuf_add_tabexpand(sb, opt, pp->color,
					     pp->expand_tabs_in_log, line,
					     linelen);
		else {
			if (pp->fmt == CMIT_FMT_MBOXRD &&
					is_mboxrd_from(line, linelen))
				strbuf_addch(sb, '>');

			append_line_with_color(sb, opt, line, linelen,
					       pp->color, GREP_CONTEXT_BODY,
					       GREP_HEADER_FIELD_MAX);
		}
		strbuf_addch(sb, '\n');
	}
}

void pretty_print_commit(struct pretty_print_context *pp,
			 const struct commit *commit,
			 struct strbuf *sb)
{
	unsigned long beginning_of_body;
	int indent = 4;
	const char *msg;
	const char *reencoded;
	const char *encoding;
	int need_8bit_cte = pp->need_8bit_cte;

	if (pp->fmt == CMIT_FMT_USERFORMAT) {
		repo_format_commit_message(the_repository, commit,
					   user_format, sb, pp);
		return;
	}

	encoding = get_log_output_encoding();
	msg = reencoded = repo_logmsg_reencode(the_repository, commit, NULL,
					       encoding);

	if (pp->fmt == CMIT_FMT_ONELINE || cmit_fmt_is_mail(pp->fmt))
		indent = 0;

	/*
	 * We need to check and emit Content-type: to mark it
	 * as 8-bit if we haven't done so.
	 */
	if (cmit_fmt_is_mail(pp->fmt) && need_8bit_cte == 0) {
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
	if (pp->fmt != CMIT_FMT_ONELINE && !cmit_fmt_is_mail(pp->fmt)) {
		strbuf_addch(sb, '\n');
	}

	/* Skip excess blank lines at the beginning of body, if any... */
	msg = skip_blank_lines(msg);

	/* These formats treat the title line specially. */
	if (pp->fmt == CMIT_FMT_ONELINE) {
		msg = format_subject(sb, msg, " ");
		strbuf_addch(sb, '\n');
	} else if (cmit_fmt_is_mail(pp->fmt))
		pp_email_subject(pp, &msg, sb, encoding, need_8bit_cte);

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
	if (cmit_fmt_is_mail(pp->fmt) && sb->len <= beginning_of_body)
		strbuf_addch(sb, '\n');

	repo_unuse_commit_buffer(the_repository, commit, reencoded);
}

void pp_commit_easy(enum cmit_fmt fmt, const struct commit *commit,
		    struct strbuf *sb)
{
	struct pretty_print_context pp = {0};
	pp.fmt = fmt;
	pretty_print_commit(&pp, commit, sb);
}
