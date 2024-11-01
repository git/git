#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "string-list.h"
#include "run-command.h"
#include "commit.h"
#include "trailer.h"
#include "list.h"
/*
 * Copyright (c) 2013, 2014 Christian Couder <chriscool@tuxfamily.org>
 */

struct trailer_info {
	/*
	 * True if there is a blank line before the location pointed to by
	 * trailer_block_start.
	 */
	int blank_line_before_trailer;

	/*
	 * Offsets to the trailer block start and end positions in the input
	 * string. If no trailer block is found, these are both set to the
	 * "true" end of the input (find_end_of_log_message()).
	 */
	size_t trailer_block_start, trailer_block_end;

	/*
	 * Array of trailers found.
	 */
	char **trailers;
	size_t trailer_nr;
};

struct conf_info {
	char *name;
	char *key;
	char *command;
	char *cmd;
	enum trailer_where where;
	enum trailer_if_exists if_exists;
	enum trailer_if_missing if_missing;
};

static struct conf_info default_conf_info;

struct trailer_item {
	struct list_head list;
	/*
	 * If this is not a trailer line, the line is stored in value
	 * (excluding the terminating newline) and token is NULL.
	 */
	char *token;
	char *value;
};

struct arg_item {
	struct list_head list;
	char *token;
	char *value;
	struct conf_info conf;
};

static LIST_HEAD(conf_head);

static const char *separators = ":";

static int configured;

#define TRAILER_ARG_STRING "$ARG"

static const char *git_generated_prefixes[] = {
	"Signed-off-by: ",
	"(cherry picked from commit ",
	NULL
};

/* Iterate over the elements of the list. */
#define list_for_each_dir(pos, head, is_reverse) \
	for (pos = is_reverse ? (head)->prev : (head)->next; \
		pos != (head); \
		pos = is_reverse ? pos->prev : pos->next)

static int after_or_end(enum trailer_where where)
{
	return (where == WHERE_AFTER) || (where == WHERE_END);
}

/*
 * Return the length of the string not including any final
 * punctuation. E.g., the input "Signed-off-by:" would return
 * 13, stripping the trailing punctuation but retaining
 * internal punctuation.
 */
static size_t token_len_without_separator(const char *token, size_t len)
{
	while (len > 0 && !isalnum(token[len - 1]))
		len--;
	return len;
}

static int same_token(struct trailer_item *a, struct arg_item *b)
{
	size_t a_len, b_len, min_len;

	if (!a->token)
		return 0;

	a_len = token_len_without_separator(a->token, strlen(a->token));
	b_len = token_len_without_separator(b->token, strlen(b->token));
	min_len = (a_len > b_len) ? b_len : a_len;

	return !strncasecmp(a->token, b->token, min_len);
}

static int same_value(struct trailer_item *a, struct arg_item *b)
{
	return !strcasecmp(a->value, b->value);
}

static int same_trailer(struct trailer_item *a, struct arg_item *b)
{
	return same_token(a, b) && same_value(a, b);
}

static inline int is_blank_line(const char *str)
{
	const char *s = str;
	while (*s && *s != '\n' && isspace(*s))
		s++;
	return !*s || *s == '\n';
}

static inline void strbuf_replace(struct strbuf *sb, const char *a, const char *b)
{
	const char *ptr = strstr(sb->buf, a);
	if (ptr)
		strbuf_splice(sb, ptr - sb->buf, strlen(a), b, strlen(b));
}

static void free_trailer_item(struct trailer_item *item)
{
	free(item->token);
	free(item->value);
	free(item);
}

static void free_arg_item(struct arg_item *item)
{
	free(item->conf.name);
	free(item->conf.key);
	free(item->conf.command);
	free(item->conf.cmd);
	free(item->token);
	free(item->value);
	free(item);
}

static char last_non_space_char(const char *s)
{
	int i;
	for (i = strlen(s) - 1; i >= 0; i--)
		if (!isspace(s[i]))
			return s[i];
	return '\0';
}

static struct trailer_item *trailer_from_arg(struct arg_item *arg_tok)
{
	struct trailer_item *new_item = xcalloc(1, sizeof(*new_item));
	new_item->token = arg_tok->token;
	new_item->value = arg_tok->value;
	arg_tok->token = arg_tok->value = NULL;
	free_arg_item(arg_tok);
	return new_item;
}

static void add_arg_to_input_list(struct trailer_item *on_tok,
				  struct arg_item *arg_tok)
{
	int aoe = after_or_end(arg_tok->conf.where);
	struct trailer_item *to_add = trailer_from_arg(arg_tok);
	if (aoe)
		list_add(&to_add->list, &on_tok->list);
	else
		list_add_tail(&to_add->list, &on_tok->list);
}

static int check_if_different(struct trailer_item *in_tok,
			      struct arg_item *arg_tok,
			      int check_all,
			      struct list_head *head)
{
	enum trailer_where where = arg_tok->conf.where;
	struct list_head *next_head;
	do {
		if (same_trailer(in_tok, arg_tok))
			return 0;
		/*
		 * if we want to add a trailer after another one,
		 * we have to check those before this one
		 */
		next_head = after_or_end(where) ? in_tok->list.prev
						: in_tok->list.next;
		if (next_head == head)
			break;
		in_tok = list_entry(next_head, struct trailer_item, list);
	} while (check_all);
	return 1;
}

static char *apply_command(struct conf_info *conf, const char *arg)
{
	struct strbuf cmd = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *result;

	if (conf->cmd) {
		strbuf_addstr(&cmd, conf->cmd);
		strvec_push(&cp.args, cmd.buf);
		if (arg)
			strvec_push(&cp.args, arg);
	} else if (conf->command) {
		strbuf_addstr(&cmd, conf->command);
		if (arg)
			strbuf_replace(&cmd, TRAILER_ARG_STRING, arg);
		strvec_push(&cp.args, cmd.buf);
	}
	strvec_pushv(&cp.env, (const char **)local_repo_env);
	cp.no_stdin = 1;
	cp.use_shell = 1;

	if (capture_command(&cp, &buf, 1024)) {
		error(_("running trailer command '%s' failed"), cmd.buf);
		strbuf_release(&buf);
		result = xstrdup("");
	} else {
		strbuf_trim(&buf);
		result = strbuf_detach(&buf, NULL);
	}

	strbuf_release(&cmd);
	return result;
}

static void apply_item_command(struct trailer_item *in_tok, struct arg_item *arg_tok)
{
	if (arg_tok->conf.command || arg_tok->conf.cmd) {
		char *value_to_free = NULL;
		char *arg;

		if (arg_tok->value && arg_tok->value[0]) {
			arg = (char *)arg_tok->value;
		} else {
			if (in_tok && in_tok->value)
				arg = xstrdup(in_tok->value);
			else
				arg = xstrdup("");
			value_to_free = arg_tok->value;
		}

		arg_tok->value = apply_command(&arg_tok->conf, arg);

		free(value_to_free);
		free(arg);
	}
}

static void apply_arg_if_exists(struct trailer_item *in_tok,
				struct arg_item *arg_tok,
				struct trailer_item *on_tok,
				struct list_head *head)
{
	switch (arg_tok->conf.if_exists) {
	case EXISTS_DO_NOTHING:
		free_arg_item(arg_tok);
		break;
	case EXISTS_REPLACE:
		apply_item_command(in_tok, arg_tok);
		add_arg_to_input_list(on_tok, arg_tok);
		list_del(&in_tok->list);
		free_trailer_item(in_tok);
		break;
	case EXISTS_ADD:
		apply_item_command(in_tok, arg_tok);
		add_arg_to_input_list(on_tok, arg_tok);
		break;
	case EXISTS_ADD_IF_DIFFERENT:
		apply_item_command(in_tok, arg_tok);
		if (check_if_different(in_tok, arg_tok, 1, head))
			add_arg_to_input_list(on_tok, arg_tok);
		else
			free_arg_item(arg_tok);
		break;
	case EXISTS_ADD_IF_DIFFERENT_NEIGHBOR:
		apply_item_command(in_tok, arg_tok);
		if (check_if_different(on_tok, arg_tok, 0, head))
			add_arg_to_input_list(on_tok, arg_tok);
		else
			free_arg_item(arg_tok);
		break;
	default:
		BUG("trailer.c: unhandled value %d",
		    arg_tok->conf.if_exists);
	}
}

static void apply_arg_if_missing(struct list_head *head,
				 struct arg_item *arg_tok)
{
	enum trailer_where where;
	struct trailer_item *to_add;

	switch (arg_tok->conf.if_missing) {
	case MISSING_DO_NOTHING:
		free_arg_item(arg_tok);
		break;
	case MISSING_ADD:
		where = arg_tok->conf.where;
		apply_item_command(NULL, arg_tok);
		to_add = trailer_from_arg(arg_tok);
		if (after_or_end(where))
			list_add_tail(&to_add->list, head);
		else
			list_add(&to_add->list, head);
		break;
	default:
		BUG("trailer.c: unhandled value %d",
		    arg_tok->conf.if_missing);
	}
}

static int find_same_and_apply_arg(struct list_head *head,
				   struct arg_item *arg_tok)
{
	struct list_head *pos;
	struct trailer_item *in_tok;
	struct trailer_item *on_tok;

	enum trailer_where where = arg_tok->conf.where;
	int middle = (where == WHERE_AFTER) || (where == WHERE_BEFORE);
	int backwards = after_or_end(where);
	struct trailer_item *start_tok;

	if (list_empty(head))
		return 0;

	start_tok = list_entry(backwards ? head->prev : head->next,
			       struct trailer_item,
			       list);

	list_for_each_dir(pos, head, backwards) {
		in_tok = list_entry(pos, struct trailer_item, list);
		if (!same_token(in_tok, arg_tok))
			continue;
		on_tok = middle ? in_tok : start_tok;
		apply_arg_if_exists(in_tok, arg_tok, on_tok, head);
		return 1;
	}
	return 0;
}

void process_trailers_lists(struct list_head *head,
			    struct list_head *arg_head)
{
	struct list_head *pos, *p;
	struct arg_item *arg_tok;

	list_for_each_safe(pos, p, arg_head) {
		int applied = 0;
		arg_tok = list_entry(pos, struct arg_item, list);

		list_del(pos);

		applied = find_same_and_apply_arg(head, arg_tok);

		if (!applied)
			apply_arg_if_missing(head, arg_tok);
	}
}

int trailer_set_where(enum trailer_where *item, const char *value)
{
	if (!value)
		*item = WHERE_DEFAULT;
	else if (!strcasecmp("after", value))
		*item = WHERE_AFTER;
	else if (!strcasecmp("before", value))
		*item = WHERE_BEFORE;
	else if (!strcasecmp("end", value))
		*item = WHERE_END;
	else if (!strcasecmp("start", value))
		*item = WHERE_START;
	else
		return -1;
	return 0;
}

int trailer_set_if_exists(enum trailer_if_exists *item, const char *value)
{
	if (!value)
		*item = EXISTS_DEFAULT;
	else if (!strcasecmp("addIfDifferent", value))
		*item = EXISTS_ADD_IF_DIFFERENT;
	else if (!strcasecmp("addIfDifferentNeighbor", value))
		*item = EXISTS_ADD_IF_DIFFERENT_NEIGHBOR;
	else if (!strcasecmp("add", value))
		*item = EXISTS_ADD;
	else if (!strcasecmp("replace", value))
		*item = EXISTS_REPLACE;
	else if (!strcasecmp("doNothing", value))
		*item = EXISTS_DO_NOTHING;
	else
		return -1;
	return 0;
}

int trailer_set_if_missing(enum trailer_if_missing *item, const char *value)
{
	if (!value)
		*item = MISSING_DEFAULT;
	else if (!strcasecmp("doNothing", value))
		*item = MISSING_DO_NOTHING;
	else if (!strcasecmp("add", value))
		*item = MISSING_ADD;
	else
		return -1;
	return 0;
}

static void duplicate_conf(struct conf_info *dst, const struct conf_info *src)
{
	*dst = *src;
	dst->name = xstrdup_or_null(src->name);
	dst->key = xstrdup_or_null(src->key);
	dst->command = xstrdup_or_null(src->command);
	dst->cmd = xstrdup_or_null(src->cmd);
}

static struct arg_item *get_conf_item(const char *name)
{
	struct list_head *pos;
	struct arg_item *item;

	/* Look up item with same name */
	list_for_each(pos, &conf_head) {
		item = list_entry(pos, struct arg_item, list);
		if (!strcasecmp(item->conf.name, name))
			return item;
	}

	/* Item does not already exists, create it */
	CALLOC_ARRAY(item, 1);
	duplicate_conf(&item->conf, &default_conf_info);
	item->conf.name = xstrdup(name);

	list_add_tail(&item->list, &conf_head);

	return item;
}

enum trailer_info_type { TRAILER_KEY, TRAILER_COMMAND, TRAILER_CMD,
			TRAILER_WHERE, TRAILER_IF_EXISTS, TRAILER_IF_MISSING };

static struct {
	const char *name;
	enum trailer_info_type type;
} trailer_config_items[] = {
	{ "key", TRAILER_KEY },
	{ "command", TRAILER_COMMAND },
	{ "cmd", TRAILER_CMD },
	{ "where", TRAILER_WHERE },
	{ "ifexists", TRAILER_IF_EXISTS },
	{ "ifmissing", TRAILER_IF_MISSING }
};

static int git_trailer_default_config(const char *conf_key, const char *value,
				      const struct config_context *ctx UNUSED,
				      void *cb UNUSED)
{
	const char *trailer_item, *variable_name;

	if (!skip_prefix(conf_key, "trailer.", &trailer_item))
		return 0;

	variable_name = strrchr(trailer_item, '.');
	if (!variable_name) {
		if (!strcmp(trailer_item, "where")) {
			if (trailer_set_where(&default_conf_info.where,
					      value) < 0)
				warning(_("unknown value '%s' for key '%s'"),
					value, conf_key);
		} else if (!strcmp(trailer_item, "ifexists")) {
			if (trailer_set_if_exists(&default_conf_info.if_exists,
						  value) < 0)
				warning(_("unknown value '%s' for key '%s'"),
					value, conf_key);
		} else if (!strcmp(trailer_item, "ifmissing")) {
			if (trailer_set_if_missing(&default_conf_info.if_missing,
						   value) < 0)
				warning(_("unknown value '%s' for key '%s'"),
					value, conf_key);
		} else if (!strcmp(trailer_item, "separators")) {
			if (!value)
				return config_error_nonbool(conf_key);
			separators = xstrdup(value);
		}
	}
	return 0;
}

static int git_trailer_config(const char *conf_key, const char *value,
			      const struct config_context *ctx UNUSED,
			      void *cb UNUSED)
{
	const char *trailer_item, *variable_name;
	struct arg_item *item;
	struct conf_info *conf;
	char *name = NULL;
	enum trailer_info_type type;
	int i;

	if (!skip_prefix(conf_key, "trailer.", &trailer_item))
		return 0;

	variable_name = strrchr(trailer_item, '.');
	if (!variable_name)
		return 0;

	variable_name++;
	for (i = 0; i < ARRAY_SIZE(trailer_config_items); i++) {
		if (strcmp(trailer_config_items[i].name, variable_name))
			continue;
		name = xstrndup(trailer_item,  variable_name - trailer_item - 1);
		type = trailer_config_items[i].type;
		break;
	}

	if (!name)
		return 0;

	item = get_conf_item(name);
	conf = &item->conf;
	free(name);

	switch (type) {
	case TRAILER_KEY:
		if (conf->key)
			warning(_("more than one %s"), conf_key);
		if (!value)
			return config_error_nonbool(conf_key);
		conf->key = xstrdup(value);
		break;
	case TRAILER_COMMAND:
		if (conf->command)
			warning(_("more than one %s"), conf_key);
		if (!value)
			return config_error_nonbool(conf_key);
		conf->command = xstrdup(value);
		break;
	case TRAILER_CMD:
		if (conf->cmd)
			warning(_("more than one %s"), conf_key);
		if (!value)
			return config_error_nonbool(conf_key);
		conf->cmd = xstrdup(value);
		break;
	case TRAILER_WHERE:
		if (trailer_set_where(&conf->where, value))
			warning(_("unknown value '%s' for key '%s'"), value, conf_key);
		break;
	case TRAILER_IF_EXISTS:
		if (trailer_set_if_exists(&conf->if_exists, value))
			warning(_("unknown value '%s' for key '%s'"), value, conf_key);
		break;
	case TRAILER_IF_MISSING:
		if (trailer_set_if_missing(&conf->if_missing, value))
			warning(_("unknown value '%s' for key '%s'"), value, conf_key);
		break;
	default:
		BUG("trailer.c: unhandled type %d", type);
	}
	return 0;
}

void trailer_config_init(void)
{
	if (configured)
		return;

	/* Default config must be setup first */
	default_conf_info.where = WHERE_END;
	default_conf_info.if_exists = EXISTS_ADD_IF_DIFFERENT_NEIGHBOR;
	default_conf_info.if_missing = MISSING_ADD;
	git_config(git_trailer_default_config, NULL);
	git_config(git_trailer_config, NULL);
	configured = 1;
}

static const char *token_from_item(struct arg_item *item, char *tok)
{
	if (item->conf.key)
		return item->conf.key;
	if (tok)
		return tok;
	return item->conf.name;
}

static int token_matches_item(const char *tok, struct arg_item *item, size_t tok_len)
{
	if (!strncasecmp(tok, item->conf.name, tok_len))
		return 1;
	return item->conf.key ? !strncasecmp(tok, item->conf.key, tok_len) : 0;
}

/*
 * If the given line is of the form
 * "<token><optional whitespace><separator>..." or "<separator>...", return the
 * location of the separator. Otherwise, return -1.  The optional whitespace
 * is allowed there primarily to allow things like "Bug #43" where <token> is
 * "Bug" and <separator> is "#".
 *
 * The separator-starts-line case (in which this function returns 0) is
 * distinguished from the non-well-formed-line case (in which this function
 * returns -1) because some callers of this function need such a distinction.
 */
static ssize_t find_separator(const char *line, const char *separators)
{
	int whitespace_found = 0;
	const char *c;
	for (c = line; *c; c++) {
		if (strchr(separators, *c))
			return c - line;
		if (!whitespace_found && (isalnum(*c) || *c == '-'))
			continue;
		if (c != line && (*c == ' ' || *c == '\t')) {
			whitespace_found = 1;
			continue;
		}
		break;
	}
	return -1;
}

/*
 * Obtain the token, value, and conf from the given trailer.
 *
 * separator_pos must not be 0, since the token cannot be an empty string.
 *
 * If separator_pos is -1, interpret the whole trailer as a token.
 */
static void parse_trailer(struct strbuf *tok, struct strbuf *val,
			 const struct conf_info **conf, const char *trailer,
			 ssize_t separator_pos)
{
	struct arg_item *item;
	size_t tok_len;
	struct list_head *pos;

	if (separator_pos != -1) {
		strbuf_add(tok, trailer, separator_pos);
		strbuf_trim(tok);
		strbuf_addstr(val, trailer + separator_pos + 1);
		strbuf_trim(val);
	} else {
		strbuf_addstr(tok, trailer);
		strbuf_trim(tok);
	}

	/* Lookup if the token matches something in the config */
	tok_len = token_len_without_separator(tok->buf, tok->len);
	if (conf)
		*conf = &default_conf_info;
	list_for_each(pos, &conf_head) {
		item = list_entry(pos, struct arg_item, list);
		if (token_matches_item(tok->buf, item, tok_len)) {
			char *tok_buf = strbuf_detach(tok, NULL);
			if (conf)
				*conf = &item->conf;
			strbuf_addstr(tok, token_from_item(item, tok_buf));
			free(tok_buf);
			break;
		}
	}
}

static struct trailer_item *add_trailer_item(struct list_head *head, char *tok,
					     char *val)
{
	struct trailer_item *new_item = xcalloc(1, sizeof(*new_item));
	new_item->token = tok;
	new_item->value = val;
	list_add_tail(&new_item->list, head);
	return new_item;
}

static void add_arg_item(struct list_head *arg_head, char *tok, char *val,
			 const struct conf_info *conf,
			 const struct new_trailer_item *new_trailer_item)
{
	struct arg_item *new_item = xcalloc(1, sizeof(*new_item));
	new_item->token = tok;
	new_item->value = val;
	duplicate_conf(&new_item->conf, conf);
	if (new_trailer_item) {
		if (new_trailer_item->where != WHERE_DEFAULT)
			new_item->conf.where = new_trailer_item->where;
		if (new_trailer_item->if_exists != EXISTS_DEFAULT)
			new_item->conf.if_exists = new_trailer_item->if_exists;
		if (new_trailer_item->if_missing != MISSING_DEFAULT)
			new_item->conf.if_missing = new_trailer_item->if_missing;
	}
	list_add_tail(&new_item->list, arg_head);
}

void parse_trailers_from_config(struct list_head *config_head)
{
	struct arg_item *item;
	struct list_head *pos;

	/* Add an arg item for each configured trailer with a command */
	list_for_each(pos, &conf_head) {
		item = list_entry(pos, struct arg_item, list);
		if (item->conf.command)
			add_arg_item(config_head,
				     xstrdup(token_from_item(item, NULL)),
				     xstrdup(""),
				     &item->conf, NULL);
	}
}

void parse_trailers_from_command_line_args(struct list_head *arg_head,
					   struct list_head *new_trailer_head)
{
	struct strbuf tok = STRBUF_INIT;
	struct strbuf val = STRBUF_INIT;
	const struct conf_info *conf;
	struct list_head *pos;

	/*
	 * In command-line arguments, '=' is accepted (in addition to the
	 * separators that are defined).
	 */
	char *cl_separators = xstrfmt("=%s", separators);

	/* Add an arg item for each trailer on the command line */
	list_for_each(pos, new_trailer_head) {
		struct new_trailer_item *tr =
			list_entry(pos, struct new_trailer_item, list);
		ssize_t separator_pos = find_separator(tr->text, cl_separators);

		if (separator_pos == 0) {
			struct strbuf sb = STRBUF_INIT;
			strbuf_addstr(&sb, tr->text);
			strbuf_trim(&sb);
			error(_("empty trailer token in trailer '%.*s'"),
			      (int) sb.len, sb.buf);
			strbuf_release(&sb);
		} else {
			parse_trailer(&tok, &val, &conf, tr->text,
				      separator_pos);
			add_arg_item(arg_head,
				     strbuf_detach(&tok, NULL),
				     strbuf_detach(&val, NULL),
				     conf, tr);
		}
	}

	free(cl_separators);
}

static const char *next_line(const char *str)
{
	const char *nl = strchrnul(str, '\n');
	return nl + !!*nl;
}

/*
 * Return the position of the start of the last line. If len is 0, return -1.
 */
static ssize_t last_line(const char *buf, size_t len)
{
	ssize_t i;
	if (len == 0)
		return -1;
	if (len == 1)
		return 0;
	/*
	 * Skip the last character (in addition to the null terminator),
	 * because if the last character is a newline, it is considered as part
	 * of the last line anyway.
	 */
	i = len - 2;

	for (; i >= 0; i--) {
		if (buf[i] == '\n')
			return i + 1;
	}
	return 0;
}

/*
 * Find the end of the log message as an offset from the start of the input
 * (where callers of this function are interested in looking for a trailers
 * block in the same input). We have to consider two categories of content that
 * can come at the end of the input which we want to ignore (because they don't
 * belong in the log message):
 *
 * (1) the "patch part" which begins with a "---" divider and has patch
 * information (like the output of git-format-patch), and
 *
 * (2) any trailing comment lines, blank lines like in the output of "git
 * commit -v", or stuff below the "cut" (scissor) line.
 *
 * As a formula, the situation looks like this:
 *
 *     INPUT = LOG MESSAGE + IGNORED
 *
 * where IGNORED can be either of the two categories described above. It may be
 * that there is nothing to ignore. Now it may be the case that the LOG MESSAGE
 * contains a trailer block, but that's not the concern of this function.
 */
static size_t find_end_of_log_message(const char *input, int no_divider)
{
	size_t end;
	const char *s;

	/* Assume the naive end of the input is already what we want. */
	end = strlen(input);

	/* Optionally skip over any patch part ("---" line and below). */
	if (!no_divider) {
		for (s = input; *s; s = next_line(s)) {
			const char *v;

			if (skip_prefix(s, "---", &v) && isspace(*v)) {
				end = s - input;
				break;
			}
		}
	}

	/* Skip over other ignorable bits. */
	return end - ignored_log_message_bytes(input, end);
}

/*
 * Return the position of the first trailer line or len if there are no
 * trailers.
 */
static size_t find_trailer_block_start(const char *buf, size_t len)
{
	const char *s;
	ssize_t end_of_title, l;
	int only_spaces = 1;
	int recognized_prefix = 0, trailer_lines = 0, non_trailer_lines = 0;
	/*
	 * Number of possible continuation lines encountered. This will be
	 * reset to 0 if we encounter a trailer (since those lines are to be
	 * considered continuations of that trailer), and added to
	 * non_trailer_lines if we encounter a non-trailer (since those lines
	 * are to be considered non-trailers).
	 */
	int possible_continuation_lines = 0;

	/* The first paragraph is the title and cannot be trailers */
	for (s = buf; s < buf + len; s = next_line(s)) {
		if (starts_with_mem(s, buf + len - s, comment_line_str))
			continue;
		if (is_blank_line(s))
			break;
	}
	end_of_title = s - buf;

	/*
	 * Get the start of the trailers by looking starting from the end for a
	 * blank line before a set of non-blank lines that (i) are all
	 * trailers, or (ii) contains at least one Git-generated trailer and
	 * consists of at least 25% trailers.
	 */
	for (l = last_line(buf, len);
	     l >= end_of_title;
	     l = last_line(buf, l)) {
		const char *bol = buf + l;
		const char **p;
		ssize_t separator_pos;

		if (starts_with_mem(bol, buf + len - bol, comment_line_str)) {
			non_trailer_lines += possible_continuation_lines;
			possible_continuation_lines = 0;
			continue;
		}
		if (is_blank_line(bol)) {
			if (only_spaces)
				continue;
			non_trailer_lines += possible_continuation_lines;
			if (recognized_prefix &&
			    trailer_lines * 3 >= non_trailer_lines)
				return next_line(bol) - buf;
			else if (trailer_lines && !non_trailer_lines)
				return next_line(bol) - buf;
			return len;
		}
		only_spaces = 0;

		for (p = git_generated_prefixes; *p; p++) {
			if (starts_with(bol, *p)) {
				trailer_lines++;
				possible_continuation_lines = 0;
				recognized_prefix = 1;
				goto continue_outer_loop;
			}
		}

		separator_pos = find_separator(bol, separators);
		if (separator_pos >= 1 && !isspace(bol[0])) {
			struct list_head *pos;

			trailer_lines++;
			possible_continuation_lines = 0;
			if (recognized_prefix)
				continue;
			list_for_each(pos, &conf_head) {
				struct arg_item *item;
				item = list_entry(pos, struct arg_item, list);
				if (token_matches_item(bol, item,
						       separator_pos)) {
					recognized_prefix = 1;
					break;
				}
			}
		} else if (isspace(bol[0]))
			possible_continuation_lines++;
		else {
			non_trailer_lines++;
			non_trailer_lines += possible_continuation_lines;
			possible_continuation_lines = 0;
		}
continue_outer_loop:
		;
	}

	return len;
}

static int ends_with_blank_line(const char *buf, size_t len)
{
	ssize_t ll = last_line(buf, len);
	if (ll < 0)
		return 0;
	return is_blank_line(buf + ll);
}

static void unfold_value(struct strbuf *val)
{
	struct strbuf out = STRBUF_INIT;
	size_t i;

	strbuf_grow(&out, val->len);
	i = 0;
	while (i < val->len) {
		char c = val->buf[i++];
		if (c == '\n') {
			/* Collapse continuation down to a single space. */
			while (i < val->len && isspace(val->buf[i]))
				i++;
			strbuf_addch(&out, ' ');
		} else {
			strbuf_addch(&out, c);
		}
	}

	/* Empty lines may have left us with whitespace cruft at the edges */
	strbuf_trim(&out);

	/* output goes back to val as if we modified it in-place */
	strbuf_swap(&out, val);
	strbuf_release(&out);
}

static struct trailer_info *trailer_info_new(void)
{
	struct trailer_info *info = xcalloc(1, sizeof(*info));
	return info;
}

static struct trailer_info *trailer_info_get(const struct process_trailer_options *opts,
					     const char *str)
{
	struct trailer_info *info = trailer_info_new();
	size_t end_of_log_message = 0, trailer_block_start = 0;
	struct strbuf **trailer_lines, **ptr;
	char **trailer_strings = NULL;
	size_t nr = 0, alloc = 0;
	char **last = NULL;

	trailer_config_init();

	end_of_log_message = find_end_of_log_message(str, opts->no_divider);
	trailer_block_start = find_trailer_block_start(str, end_of_log_message);

	trailer_lines = strbuf_split_buf(str + trailer_block_start,
					 end_of_log_message - trailer_block_start,
					 '\n',
					 0);
	for (ptr = trailer_lines; *ptr; ptr++) {
		if (last && isspace((*ptr)->buf[0])) {
			struct strbuf sb = STRBUF_INIT;
			strbuf_attach(&sb, *last, strlen(*last), strlen(*last));
			strbuf_addbuf(&sb, *ptr);
			*last = strbuf_detach(&sb, NULL);
			continue;
		}
		ALLOC_GROW(trailer_strings, nr + 1, alloc);
		trailer_strings[nr] = strbuf_detach(*ptr, NULL);
		last = find_separator(trailer_strings[nr], separators) >= 1
			? &trailer_strings[nr]
			: NULL;
		nr++;
	}
	strbuf_list_free(trailer_lines);

	info->blank_line_before_trailer = ends_with_blank_line(str,
							       trailer_block_start);
	info->trailer_block_start = trailer_block_start;
	info->trailer_block_end = end_of_log_message;
	info->trailers = trailer_strings;
	info->trailer_nr = nr;

	return info;
}

/*
 * Parse trailers in "str", populating the trailer info and "trailer_objects"
 * linked list structure.
 */
struct trailer_info *parse_trailers(const struct process_trailer_options *opts,
				    const char *str,
				    struct list_head *trailer_objects)
{
	struct trailer_info *info;
	struct strbuf tok = STRBUF_INIT;
	struct strbuf val = STRBUF_INIT;
	size_t i;

	info = trailer_info_get(opts, str);

	for (i = 0; i < info->trailer_nr; i++) {
		int separator_pos;
		char *trailer = info->trailers[i];
		if (starts_with(trailer, comment_line_str))
			continue;
		separator_pos = find_separator(trailer, separators);
		if (separator_pos >= 1) {
			parse_trailer(&tok, &val, NULL, trailer,
				      separator_pos);
			if (opts->unfold)
				unfold_value(&val);
			add_trailer_item(trailer_objects,
					 strbuf_detach(&tok, NULL),
					 strbuf_detach(&val, NULL));
		} else if (!opts->only_trailers) {
			strbuf_addstr(&val, trailer);
			strbuf_strip_suffix(&val, "\n");
			add_trailer_item(trailer_objects,
					 NULL,
					 strbuf_detach(&val, NULL));
		}
	}

	return info;
}

void free_trailers(struct list_head *trailers)
{
	struct list_head *pos, *p;
	list_for_each_safe(pos, p, trailers) {
		list_del(pos);
		free_trailer_item(list_entry(pos, struct trailer_item, list));
	}
}

size_t trailer_block_start(struct trailer_info *info)
{
	return info->trailer_block_start;
}

size_t trailer_block_end(struct trailer_info *info)
{
	return info->trailer_block_end;
}

int blank_line_before_trailer_block(struct trailer_info *info)
{
	return info->blank_line_before_trailer;
}

void trailer_info_release(struct trailer_info *info)
{
	size_t i;
	for (i = 0; i < info->trailer_nr; i++)
		free(info->trailers[i]);
	free(info->trailers);
	free(info);
}

void format_trailers(const struct process_trailer_options *opts,
		     struct list_head *trailers,
		     struct strbuf *out)
{
	struct strbuf tok = STRBUF_INIT;
	struct strbuf val = STRBUF_INIT;
	size_t origlen = out->len;
	struct list_head *pos;
	struct trailer_item *item;


	list_for_each(pos, trailers) {
		item = list_entry(pos, struct trailer_item, list);
		if (item->token) {
			strbuf_reset(&tok);
			strbuf_addstr(&tok, item->token);
			strbuf_reset(&val);
			strbuf_addstr(&val, item->value);

			/*
			 * Skip key/value pairs where the value was empty. This
			 * can happen from trailers specified without a
			 * separator, like `--trailer "Reviewed-by"` (no
			 * corresponding value).
			 */
			if (opts->trim_empty && !strlen(item->value))
				continue;

			if (!opts->filter || opts->filter(&tok, opts->filter_data)) {
				if (opts->separator && out->len != origlen)
					strbuf_addbuf(out, opts->separator);
				if (!opts->value_only)
					strbuf_addbuf(out, &tok);
				if (!opts->key_only && !opts->value_only) {
					if (opts->key_value_separator)
						strbuf_addbuf(out, opts->key_value_separator);
					else {
						char c = last_non_space_char(tok.buf);
						if (c && !strchr(separators, c))
							strbuf_addf(out, "%c ", separators[0]);
					}
				}
				if (!opts->key_only)
					strbuf_addbuf(out, &val);
				if (!opts->separator)
					strbuf_addch(out, '\n');
			}
		} else if (!opts->only_trailers) {
			if (opts->separator && out->len != origlen) {
				strbuf_addbuf(out, opts->separator);
			}
			strbuf_addstr(out, item->value);
			if (opts->separator)
				strbuf_rtrim(out);
			else
				strbuf_addch(out, '\n');
		}
	}

	strbuf_release(&tok);
	strbuf_release(&val);
}

void format_trailers_from_commit(const struct process_trailer_options *opts,
				 const char *msg,
				 struct strbuf *out)
{
	LIST_HEAD(trailer_objects);
	struct trailer_info *info = parse_trailers(opts, msg, &trailer_objects);

	/* If we want the whole block untouched, we can take the fast path. */
	if (!opts->only_trailers && !opts->unfold && !opts->filter &&
	    !opts->separator && !opts->key_only && !opts->value_only &&
	    !opts->key_value_separator) {
		strbuf_add(out, msg + info->trailer_block_start,
			   info->trailer_block_end - info->trailer_block_start);
	} else
		format_trailers(opts, &trailer_objects, out);

	free_trailers(&trailer_objects);
	trailer_info_release(info);
}

void trailer_iterator_init(struct trailer_iterator *iter, const char *msg)
{
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
	strbuf_init(&iter->key, 0);
	strbuf_init(&iter->val, 0);
	opts.no_divider = 1;
	iter->internal.info = trailer_info_get(&opts, msg);
	iter->internal.cur = 0;
}

int trailer_iterator_advance(struct trailer_iterator *iter)
{
	if (iter->internal.cur < iter->internal.info->trailer_nr) {
		char *line = iter->internal.info->trailers[iter->internal.cur++];
		int separator_pos = find_separator(line, separators);

		iter->raw = line;
		strbuf_reset(&iter->key);
		strbuf_reset(&iter->val);
		parse_trailer(&iter->key, &iter->val, NULL,
			      line, separator_pos);
		/* Always unfold values during iteration. */
		unfold_value(&iter->val);
		return 1;
	}
	return 0;
}

void trailer_iterator_release(struct trailer_iterator *iter)
{
	trailer_info_release(iter->internal.info);
	strbuf_release(&iter->val);
	strbuf_release(&iter->key);
}

int amend_file_with_trailers(const char *path, const struct strvec *trailer_args)
{
	struct child_process run_trailer = CHILD_PROCESS_INIT;

	run_trailer.git_cmd = 1;
	strvec_pushl(&run_trailer.args, "interpret-trailers",
		     "--in-place", "--no-divider",
		     path, NULL);
	strvec_pushv(&run_trailer.args, trailer_args->v);
	return run_command(&run_trailer);
}
