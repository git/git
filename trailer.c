#include "cache.h"
#include "string-list.h"
#include "run-command.h"
#include "commit.h"
#include "tempfile.h"
#include "trailer.h"
#include "list.h"
/*
 * Copyright (c) 2013, 2014 Christian Couder <chriscool@tuxfamily.org>
 */

enum action_where { WHERE_END, WHERE_AFTER, WHERE_BEFORE, WHERE_START };
enum action_if_exists { EXISTS_ADD_IF_DIFFERENT_NEIGHBOR, EXISTS_ADD_IF_DIFFERENT,
			EXISTS_ADD, EXISTS_REPLACE, EXISTS_DO_NOTHING };
enum action_if_missing { MISSING_ADD, MISSING_DO_NOTHING };

struct conf_info {
	char *name;
	char *key;
	char *command;
	enum action_where where;
	enum action_if_exists if_exists;
	enum action_if_missing if_missing;
};

static struct conf_info default_conf_info;

struct trailer_item {
	struct list_head list;
	char *token;
	char *value;
	struct conf_info conf;
};

static LIST_HEAD(conf_head);

static char *separators = ":";

#define TRAILER_ARG_STRING "$ARG"

/* Iterate over the elements of the list. */
#define list_for_each_dir(pos, head, is_reverse) \
	for (pos = is_reverse ? (head)->prev : (head)->next; \
		pos != (head); \
		pos = is_reverse ? pos->prev : pos->next)

static int after_or_end(enum action_where where)
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

static int same_token(struct trailer_item *a, struct trailer_item *b)
{
	size_t a_len = token_len_without_separator(a->token, strlen(a->token));
	size_t b_len = token_len_without_separator(b->token, strlen(b->token));
	size_t min_len = (a_len > b_len) ? b_len : a_len;

	return !strncasecmp(a->token, b->token, min_len);
}

static int same_value(struct trailer_item *a, struct trailer_item *b)
{
	return !strcasecmp(a->value, b->value);
}

static int same_trailer(struct trailer_item *a, struct trailer_item *b)
{
	return same_token(a, b) && same_value(a, b);
}

static inline int contains_only_spaces(const char *str)
{
	const char *s = str;
	while (*s && isspace(*s))
		s++;
	return !*s;
}

static inline void strbuf_replace(struct strbuf *sb, const char *a, const char *b)
{
	const char *ptr = strstr(sb->buf, a);
	if (ptr)
		strbuf_splice(sb, ptr - sb->buf, strlen(a), b, strlen(b));
}

static void free_trailer_item(struct trailer_item *item)
{
	free(item->conf.name);
	free(item->conf.key);
	free(item->conf.command);
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

static void print_tok_val(FILE *outfile, const char *tok, const char *val)
{
	char c = last_non_space_char(tok);
	if (!c)
		return;
	if (strchr(separators, c))
		fprintf(outfile, "%s%s\n", tok, val);
	else
		fprintf(outfile, "%s%c %s\n", tok, separators[0], val);
}

static void print_all(FILE *outfile, struct list_head *head, int trim_empty)
{
	struct list_head *pos;
	struct trailer_item *item;
	list_for_each(pos, head) {
		item = list_entry(pos, struct trailer_item, list);
		if (!trim_empty || strlen(item->value) > 0)
			print_tok_val(outfile, item->token, item->value);
	}
}

static void add_arg_to_input_list(struct trailer_item *on_tok,
				  struct trailer_item *arg_tok)
{
	if (after_or_end(arg_tok->conf.where))
		list_add(&arg_tok->list, &on_tok->list);
	else
		list_add_tail(&arg_tok->list, &on_tok->list);
}

static int check_if_different(struct trailer_item *in_tok,
			      struct trailer_item *arg_tok,
			      int check_all,
			      struct list_head *head)
{
	enum action_where where = arg_tok->conf.where;
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

static char *apply_command(const char *command, const char *arg)
{
	struct strbuf cmd = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *argv[] = {NULL, NULL};
	char *result;

	strbuf_addstr(&cmd, command);
	if (arg)
		strbuf_replace(&cmd, TRAILER_ARG_STRING, arg);

	argv[0] = cmd.buf;
	cp.argv = argv;
	cp.env = local_repo_env;
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

static void apply_item_command(struct trailer_item *in_tok, struct trailer_item *arg_tok)
{
	if (arg_tok->conf.command) {
		const char *arg;
		if (arg_tok->value && arg_tok->value[0]) {
			arg = arg_tok->value;
		} else {
			if (in_tok && in_tok->value)
				arg = xstrdup(in_tok->value);
			else
				arg = xstrdup("");
		}
		arg_tok->value = apply_command(arg_tok->conf.command, arg);
		free((char *)arg);
	}
}

static void apply_arg_if_exists(struct trailer_item *in_tok,
				struct trailer_item *arg_tok,
				struct trailer_item *on_tok,
				struct list_head *head)
{
	switch (arg_tok->conf.if_exists) {
	case EXISTS_DO_NOTHING:
		free_trailer_item(arg_tok);
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
			free_trailer_item(arg_tok);
		break;
	case EXISTS_ADD_IF_DIFFERENT_NEIGHBOR:
		apply_item_command(in_tok, arg_tok);
		if (check_if_different(on_tok, arg_tok, 0, head))
			add_arg_to_input_list(on_tok, arg_tok);
		else
			free_trailer_item(arg_tok);
		break;
	}
}

static void apply_arg_if_missing(struct list_head *head,
				 struct trailer_item *arg_tok)
{
	enum action_where where;

	switch (arg_tok->conf.if_missing) {
	case MISSING_DO_NOTHING:
		free_trailer_item(arg_tok);
		break;
	case MISSING_ADD:
		where = arg_tok->conf.where;
		apply_item_command(NULL, arg_tok);
		if (after_or_end(where))
			list_add_tail(&arg_tok->list, head);
		else
			list_add(&arg_tok->list, head);
	}
}

static int find_same_and_apply_arg(struct list_head *head,
				   struct trailer_item *arg_tok)
{
	struct list_head *pos;
	struct trailer_item *in_tok;
	struct trailer_item *on_tok;

	enum action_where where = arg_tok->conf.where;
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

static void process_trailers_lists(struct list_head *head,
				   struct list_head *arg_head)
{
	struct list_head *pos, *p;
	struct trailer_item *arg_tok;

	list_for_each_safe(pos, p, arg_head) {
		int applied = 0;
		arg_tok = list_entry(pos, struct trailer_item, list);

		list_del(pos);

		applied = find_same_and_apply_arg(head, arg_tok);

		if (!applied)
			apply_arg_if_missing(head, arg_tok);
	}
}

static int set_where(struct conf_info *item, const char *value)
{
	if (!strcasecmp("after", value))
		item->where = WHERE_AFTER;
	else if (!strcasecmp("before", value))
		item->where = WHERE_BEFORE;
	else if (!strcasecmp("end", value))
		item->where = WHERE_END;
	else if (!strcasecmp("start", value))
		item->where = WHERE_START;
	else
		return -1;
	return 0;
}

static int set_if_exists(struct conf_info *item, const char *value)
{
	if (!strcasecmp("addIfDifferent", value))
		item->if_exists = EXISTS_ADD_IF_DIFFERENT;
	else if (!strcasecmp("addIfDifferentNeighbor", value))
		item->if_exists = EXISTS_ADD_IF_DIFFERENT_NEIGHBOR;
	else if (!strcasecmp("add", value))
		item->if_exists = EXISTS_ADD;
	else if (!strcasecmp("replace", value))
		item->if_exists = EXISTS_REPLACE;
	else if (!strcasecmp("doNothing", value))
		item->if_exists = EXISTS_DO_NOTHING;
	else
		return -1;
	return 0;
}

static int set_if_missing(struct conf_info *item, const char *value)
{
	if (!strcasecmp("doNothing", value))
		item->if_missing = MISSING_DO_NOTHING;
	else if (!strcasecmp("add", value))
		item->if_missing = MISSING_ADD;
	else
		return -1;
	return 0;
}

static void duplicate_conf(struct conf_info *dst, const struct conf_info *src)
{
	*dst = *src;
	if (src->name)
		dst->name = xstrdup(src->name);
	if (src->key)
		dst->key = xstrdup(src->key);
	if (src->command)
		dst->command = xstrdup(src->command);
}

static struct trailer_item *get_conf_item(const char *name)
{
	struct list_head *pos;
	struct trailer_item *item;

	/* Look up item with same name */
	list_for_each(pos, &conf_head) {
		item = list_entry(pos, struct trailer_item, list);
		if (!strcasecmp(item->conf.name, name))
			return item;
	}

	/* Item does not already exists, create it */
	item = xcalloc(sizeof(struct trailer_item), 1);
	duplicate_conf(&item->conf, &default_conf_info);
	item->conf.name = xstrdup(name);

	list_add_tail(&item->list, &conf_head);

	return item;
}

enum trailer_info_type { TRAILER_KEY, TRAILER_COMMAND, TRAILER_WHERE,
			 TRAILER_IF_EXISTS, TRAILER_IF_MISSING };

static struct {
	const char *name;
	enum trailer_info_type type;
} trailer_config_items[] = {
	{ "key", TRAILER_KEY },
	{ "command", TRAILER_COMMAND },
	{ "where", TRAILER_WHERE },
	{ "ifexists", TRAILER_IF_EXISTS },
	{ "ifmissing", TRAILER_IF_MISSING }
};

static int git_trailer_default_config(const char *conf_key, const char *value, void *cb)
{
	const char *trailer_item, *variable_name;

	if (!skip_prefix(conf_key, "trailer.", &trailer_item))
		return 0;

	variable_name = strrchr(trailer_item, '.');
	if (!variable_name) {
		if (!strcmp(trailer_item, "where")) {
			if (set_where(&default_conf_info, value) < 0)
				warning(_("unknown value '%s' for key '%s'"),
					value, conf_key);
		} else if (!strcmp(trailer_item, "ifexists")) {
			if (set_if_exists(&default_conf_info, value) < 0)
				warning(_("unknown value '%s' for key '%s'"),
					value, conf_key);
		} else if (!strcmp(trailer_item, "ifmissing")) {
			if (set_if_missing(&default_conf_info, value) < 0)
				warning(_("unknown value '%s' for key '%s'"),
					value, conf_key);
		} else if (!strcmp(trailer_item, "separators")) {
			separators = xstrdup(value);
		}
	}
	return 0;
}

static int git_trailer_config(const char *conf_key, const char *value, void *cb)
{
	const char *trailer_item, *variable_name;
	struct trailer_item *item;
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
		conf->key = xstrdup(value);
		break;
	case TRAILER_COMMAND:
		if (conf->command)
			warning(_("more than one %s"), conf_key);
		conf->command = xstrdup(value);
		break;
	case TRAILER_WHERE:
		if (set_where(conf, value))
			warning(_("unknown value '%s' for key '%s'"), value, conf_key);
		break;
	case TRAILER_IF_EXISTS:
		if (set_if_exists(conf, value))
			warning(_("unknown value '%s' for key '%s'"), value, conf_key);
		break;
	case TRAILER_IF_MISSING:
		if (set_if_missing(conf, value))
			warning(_("unknown value '%s' for key '%s'"), value, conf_key);
		break;
	default:
		die("BUG: trailer.c: unhandled type %d", type);
	}
	return 0;
}

static int parse_trailer(struct strbuf *tok, struct strbuf *val, const char *trailer)
{
	size_t len;
	struct strbuf seps = STRBUF_INIT;
	strbuf_addstr(&seps, separators);
	strbuf_addch(&seps, '=');
	len = strcspn(trailer, seps.buf);
	strbuf_release(&seps);
	if (len == 0) {
		int l = strlen(trailer);
		while (l > 0 && isspace(trailer[l - 1]))
			l--;
		return error(_("empty trailer token in trailer '%.*s'"), l, trailer);
	}
	if (len < strlen(trailer)) {
		strbuf_add(tok, trailer, len);
		strbuf_trim(tok);
		strbuf_addstr(val, trailer + len + 1);
		strbuf_trim(val);
	} else {
		strbuf_addstr(tok, trailer);
		strbuf_trim(tok);
	}
	return 0;
}

static const char *token_from_item(struct trailer_item *item, char *tok)
{
	if (item->conf.key)
		return item->conf.key;
	if (tok)
		return tok;
	return item->conf.name;
}

static struct trailer_item *new_trailer_item(struct trailer_item *conf_item,
					     char *tok, char *val)
{
	struct trailer_item *new = xcalloc(sizeof(*new), 1);
	new->value = val ? val : xstrdup("");

	if (conf_item) {
		duplicate_conf(&new->conf, &conf_item->conf);
		new->token = xstrdup(token_from_item(conf_item, tok));
		free(tok);
	} else {
		duplicate_conf(&new->conf, &default_conf_info);
		new->token = tok;
	}

	return new;
}

static int token_matches_item(const char *tok, struct trailer_item *item, int tok_len)
{
	if (!strncasecmp(tok, item->conf.name, tok_len))
		return 1;
	return item->conf.key ? !strncasecmp(tok, item->conf.key, tok_len) : 0;
}

static struct trailer_item *create_trailer_item(const char *string)
{
	struct strbuf tok = STRBUF_INIT;
	struct strbuf val = STRBUF_INIT;
	struct trailer_item *item;
	int tok_len;
	struct list_head *pos;

	if (parse_trailer(&tok, &val, string))
		return NULL;

	tok_len = token_len_without_separator(tok.buf, tok.len);

	/* Lookup if the token matches something in the config */
	list_for_each(pos, &conf_head) {
		item = list_entry(pos, struct trailer_item, list);
		if (token_matches_item(tok.buf, item, tok_len))
			return new_trailer_item(item,
						strbuf_detach(&tok, NULL),
						strbuf_detach(&val, NULL));
	}

	return new_trailer_item(NULL,
				strbuf_detach(&tok, NULL),
				strbuf_detach(&val, NULL));
}

static void add_trailer_item(struct list_head *head, struct trailer_item *new)
{
	if (!new)
		return;
	list_add_tail(&new->list, head);
}

static void process_command_line_args(struct list_head *arg_head,
				      struct string_list *trailers)
{
	struct string_list_item *tr;
	struct trailer_item *item;
	struct list_head *pos;

	/* Add a trailer item for each configured trailer with a command */
	list_for_each(pos, &conf_head) {
		item = list_entry(pos, struct trailer_item, list);
		if (item->conf.command) {
			struct trailer_item *new = new_trailer_item(item, NULL, NULL);
			add_trailer_item(arg_head, new);
		}
	}

	/* Add a trailer item for each trailer on the command line */
	for_each_string_list_item(tr, trailers) {
		struct trailer_item *new = create_trailer_item(tr->string);
		add_trailer_item(arg_head, new);
	}
}

static struct strbuf **read_input_file(const char *file)
{
	struct strbuf **lines;
	struct strbuf sb = STRBUF_INIT;

	if (file) {
		if (strbuf_read_file(&sb, file, 0) < 0)
			die_errno(_("could not read input file '%s'"), file);
	} else {
		if (strbuf_read(&sb, fileno(stdin), 0) < 0)
			die_errno(_("could not read from stdin"));
	}

	lines = strbuf_split(&sb, '\n');

	strbuf_release(&sb);

	return lines;
}

/*
 * Return the (0 based) index of the start of the patch or the line
 * count if there is no patch in the message.
 */
static int find_patch_start(struct strbuf **lines, int count)
{
	int i;

	/* Get the start of the patch part if any */
	for (i = 0; i < count; i++) {
		if (starts_with(lines[i]->buf, "---"))
			return i;
	}

	return count;
}

/*
 * Return the (0 based) index of the first trailer line or count if
 * there are no trailers. Trailers are searched only in the lines from
 * index (count - 1) down to index 0.
 */
static int find_trailer_start(struct strbuf **lines, int count)
{
	int start, end_of_title, only_spaces = 1;

	/* The first paragraph is the title and cannot be trailers */
	for (start = 0; start < count; start++) {
		if (lines[start]->buf[0] == comment_line_char)
			continue;
		if (contains_only_spaces(lines[start]->buf))
			break;
	}
	end_of_title = start;

	/*
	 * Get the start of the trailers by looking starting from the end
	 * for a line with only spaces before lines with one separator.
	 */
	for (start = count - 1; start >= end_of_title; start--) {
		if (lines[start]->buf[0] == comment_line_char)
			continue;
		if (contains_only_spaces(lines[start]->buf)) {
			if (only_spaces)
				continue;
			return start + 1;
		}
		if (strcspn(lines[start]->buf, separators) < lines[start]->len) {
			if (only_spaces)
				only_spaces = 0;
			continue;
		}
		return count;
	}

	return only_spaces ? count : 0;
}

/* Get the index of the end of the trailers */
static int find_trailer_end(struct strbuf **lines, int patch_start)
{
	struct strbuf sb = STRBUF_INIT;
	int i, ignore_bytes;

	for (i = 0; i < patch_start; i++)
		strbuf_addbuf(&sb, lines[i]);
	ignore_bytes = ignore_non_trailer(&sb);
	strbuf_release(&sb);
	for (i = patch_start - 1; i >= 0 && ignore_bytes > 0; i--)
		ignore_bytes -= lines[i]->len;

	return i + 1;
}

static int has_blank_line_before(struct strbuf **lines, int start)
{
	for (;start >= 0; start--) {
		if (lines[start]->buf[0] == comment_line_char)
			continue;
		return contains_only_spaces(lines[start]->buf);
	}
	return 0;
}

static void print_lines(FILE *outfile, struct strbuf **lines, int start, int end)
{
	int i;
	for (i = start; lines[i] && i < end; i++)
		fprintf(outfile, "%s", lines[i]->buf);
}

static int process_input_file(FILE *outfile,
			      struct strbuf **lines,
			      struct list_head *head)
{
	int count = 0;
	int patch_start, trailer_start, trailer_end, i;

	/* Get the line count */
	while (lines[count])
		count++;

	patch_start = find_patch_start(lines, count);
	trailer_end = find_trailer_end(lines, patch_start);
	trailer_start = find_trailer_start(lines, trailer_end);

	/* Print lines before the trailers as is */
	print_lines(outfile, lines, 0, trailer_start);

	if (!has_blank_line_before(lines, trailer_start - 1))
		fprintf(outfile, "\n");

	/* Parse trailer lines */
	for (i = trailer_start; i < trailer_end; i++) {
		if (lines[i]->buf[0] != comment_line_char) {
			struct trailer_item *new = create_trailer_item(lines[i]->buf);
			add_trailer_item(head, new);
		}
	}

	return trailer_end;
}

static void free_all(struct list_head *head)
{
	struct list_head *pos, *p;
	list_for_each_safe(pos, p, head) {
		list_del(pos);
		free_trailer_item(list_entry(pos, struct trailer_item, list));
	}
}

static struct tempfile trailers_tempfile;

static FILE *create_in_place_tempfile(const char *file)
{
	struct stat st;
	struct strbuf template = STRBUF_INIT;
	const char *tail;
	FILE *outfile;

	if (stat(file, &st))
		die_errno(_("could not stat %s"), file);
	if (!S_ISREG(st.st_mode))
		die(_("file %s is not a regular file"), file);
	if (!(st.st_mode & S_IWUSR))
		die(_("file %s is not writable by user"), file);

	/* Create temporary file in the same directory as the original */
	tail = strrchr(file, '/');
	if (tail != NULL)
		strbuf_add(&template, file, tail - file + 1);
	strbuf_addstr(&template, "git-interpret-trailers-XXXXXX");

	xmks_tempfile_m(&trailers_tempfile, template.buf, st.st_mode);
	strbuf_release(&template);
	outfile = fdopen_tempfile(&trailers_tempfile, "w");
	if (!outfile)
		die_errno(_("could not open temporary file"));

	return outfile;
}

void process_trailers(const char *file, int in_place, int trim_empty, struct string_list *trailers)
{
	LIST_HEAD(head);
	LIST_HEAD(arg_head);
	struct strbuf **lines;
	int trailer_end;
	FILE *outfile = stdout;

	/* Default config must be setup first */
	git_config(git_trailer_default_config, NULL);
	git_config(git_trailer_config, NULL);

	lines = read_input_file(file);

	if (in_place)
		outfile = create_in_place_tempfile(file);

	/* Print the lines before the trailers */
	trailer_end = process_input_file(outfile, lines, &head);

	process_command_line_args(&arg_head, trailers);

	process_trailers_lists(&head, &arg_head);

	print_all(outfile, &head, trim_empty);

	free_all(&head);

	/* Print the lines after the trailers as is */
	print_lines(outfile, lines, trailer_end, INT_MAX);

	if (in_place)
		if (rename_tempfile(&trailers_tempfile, file))
			die_errno(_("could not rename temporary file to %s"), file);

	strbuf_list_free(lines);
}
