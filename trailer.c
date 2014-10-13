#include "cache.h"
#include "string-list.h"
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
	struct trailer_item *previous;
	struct trailer_item *next;
	const char *token;
	const char *value;
	struct conf_info conf;
};

static struct trailer_item *first_conf_item;

static char *separators = ":";

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

static void free_trailer_item(struct trailer_item *item)
{
	free(item->conf.name);
	free(item->conf.key);
	free(item->conf.command);
	free((char *)item->token);
	free((char *)item->value);
	free(item);
}

static void update_last(struct trailer_item **last)
{
	if (*last)
		while ((*last)->next != NULL)
			*last = (*last)->next;
}

static void update_first(struct trailer_item **first)
{
	if (*first)
		while ((*first)->previous != NULL)
			*first = (*first)->previous;
}

static void add_arg_to_input_list(struct trailer_item *on_tok,
				  struct trailer_item *arg_tok,
				  struct trailer_item **first,
				  struct trailer_item **last)
{
	if (after_or_end(arg_tok->conf.where)) {
		arg_tok->next = on_tok->next;
		on_tok->next = arg_tok;
		arg_tok->previous = on_tok;
		if (arg_tok->next)
			arg_tok->next->previous = arg_tok;
		update_last(last);
	} else {
		arg_tok->previous = on_tok->previous;
		on_tok->previous = arg_tok;
		arg_tok->next = on_tok;
		if (arg_tok->previous)
			arg_tok->previous->next = arg_tok;
		update_first(first);
	}
}

static int check_if_different(struct trailer_item *in_tok,
			      struct trailer_item *arg_tok,
			      int check_all)
{
	enum action_where where = arg_tok->conf.where;
	do {
		if (!in_tok)
			return 1;
		if (same_trailer(in_tok, arg_tok))
			return 0;
		/*
		 * if we want to add a trailer after another one,
		 * we have to check those before this one
		 */
		in_tok = after_or_end(where) ? in_tok->previous : in_tok->next;
	} while (check_all);
	return 1;
}

static void remove_from_list(struct trailer_item *item,
			     struct trailer_item **first,
			     struct trailer_item **last)
{
	struct trailer_item *next = item->next;
	struct trailer_item *previous = item->previous;

	if (next) {
		item->next->previous = previous;
		item->next = NULL;
	} else if (last)
		*last = previous;

	if (previous) {
		item->previous->next = next;
		item->previous = NULL;
	} else if (first)
		*first = next;
}

static struct trailer_item *remove_first(struct trailer_item **first)
{
	struct trailer_item *item = *first;
	*first = item->next;
	if (item->next) {
		item->next->previous = NULL;
		item->next = NULL;
	}
	return item;
}

static void apply_arg_if_exists(struct trailer_item *in_tok,
				struct trailer_item *arg_tok,
				struct trailer_item *on_tok,
				struct trailer_item **in_tok_first,
				struct trailer_item **in_tok_last)
{
	switch (arg_tok->conf.if_exists) {
	case EXISTS_DO_NOTHING:
		free_trailer_item(arg_tok);
		break;
	case EXISTS_REPLACE:
		add_arg_to_input_list(on_tok, arg_tok,
				      in_tok_first, in_tok_last);
		remove_from_list(in_tok, in_tok_first, in_tok_last);
		free_trailer_item(in_tok);
		break;
	case EXISTS_ADD:
		add_arg_to_input_list(on_tok, arg_tok,
				      in_tok_first, in_tok_last);
		break;
	case EXISTS_ADD_IF_DIFFERENT:
		if (check_if_different(in_tok, arg_tok, 1))
			add_arg_to_input_list(on_tok, arg_tok,
					      in_tok_first, in_tok_last);
		else
			free_trailer_item(arg_tok);
		break;
	case EXISTS_ADD_IF_DIFFERENT_NEIGHBOR:
		if (check_if_different(on_tok, arg_tok, 0))
			add_arg_to_input_list(on_tok, arg_tok,
					      in_tok_first, in_tok_last);
		else
			free_trailer_item(arg_tok);
		break;
	}
}

static void apply_arg_if_missing(struct trailer_item **in_tok_first,
				 struct trailer_item **in_tok_last,
				 struct trailer_item *arg_tok)
{
	struct trailer_item **in_tok;
	enum action_where where;

	switch (arg_tok->conf.if_missing) {
	case MISSING_DO_NOTHING:
		free_trailer_item(arg_tok);
		break;
	case MISSING_ADD:
		where = arg_tok->conf.where;
		in_tok = after_or_end(where) ? in_tok_last : in_tok_first;
		if (*in_tok) {
			add_arg_to_input_list(*in_tok, arg_tok,
					      in_tok_first, in_tok_last);
		} else {
			*in_tok_first = arg_tok;
			*in_tok_last = arg_tok;
		}
		break;
	}
}

static int find_same_and_apply_arg(struct trailer_item **in_tok_first,
				   struct trailer_item **in_tok_last,
				   struct trailer_item *arg_tok)
{
	struct trailer_item *in_tok;
	struct trailer_item *on_tok;
	struct trailer_item *following_tok;

	enum action_where where = arg_tok->conf.where;
	int middle = (where == WHERE_AFTER) || (where == WHERE_BEFORE);
	int backwards = after_or_end(where);
	struct trailer_item *start_tok = backwards ? *in_tok_last : *in_tok_first;

	for (in_tok = start_tok; in_tok; in_tok = following_tok) {
		following_tok = backwards ? in_tok->previous : in_tok->next;
		if (!same_token(in_tok, arg_tok))
			continue;
		on_tok = middle ? in_tok : start_tok;
		apply_arg_if_exists(in_tok, arg_tok, on_tok,
				    in_tok_first, in_tok_last);
		return 1;
	}
	return 0;
}

static void process_trailers_lists(struct trailer_item **in_tok_first,
				   struct trailer_item **in_tok_last,
				   struct trailer_item **arg_tok_first)
{
	struct trailer_item *arg_tok;
	struct trailer_item *next_arg;

	if (!*arg_tok_first)
		return;

	for (arg_tok = *arg_tok_first; arg_tok; arg_tok = next_arg) {
		int applied = 0;

		next_arg = arg_tok->next;
		remove_from_list(arg_tok, arg_tok_first, NULL);

		applied = find_same_and_apply_arg(in_tok_first,
						  in_tok_last,
						  arg_tok);

		if (!applied)
			apply_arg_if_missing(in_tok_first,
					     in_tok_last,
					     arg_tok);
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

static void duplicate_conf(struct conf_info *dst, struct conf_info *src)
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
	struct trailer_item *item;
	struct trailer_item *previous;

	/* Look up item with same name */
	for (previous = NULL, item = first_conf_item;
	     item;
	     previous = item, item = item->next) {
		if (!strcasecmp(item->conf.name, name))
			return item;
	}

	/* Item does not already exists, create it */
	item = xcalloc(sizeof(struct trailer_item), 1);
	duplicate_conf(&item->conf, &default_conf_info);
	item->conf.name = xstrdup(name);

	if (!previous)
		first_conf_item = item;
	else {
		previous->next = item;
		item->previous = previous;
	}

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
		die("internal bug in trailer.c");
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
	if (len == 0)
		return error(_("empty trailer token in trailer '%s'"), trailer);
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
	new->value = val;

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

	if (parse_trailer(&tok, &val, string))
		return NULL;

	tok_len = token_len_without_separator(tok.buf, tok.len);

	/* Lookup if the token matches something in the config */
	for (item = first_conf_item; item; item = item->next) {
		if (token_matches_item(tok.buf, item, tok_len))
			return new_trailer_item(item,
						strbuf_detach(&tok, NULL),
						strbuf_detach(&val, NULL));
	}

	return new_trailer_item(NULL,
				strbuf_detach(&tok, NULL),
				strbuf_detach(&val, NULL));
}

static void add_trailer_item(struct trailer_item **first,
			     struct trailer_item **last,
			     struct trailer_item *new)
{
	if (!new)
		return;
	if (!*last) {
		*first = new;
		*last = new;
	} else {
		(*last)->next = new;
		new->previous = *last;
		*last = new;
	}
}

static struct trailer_item *process_command_line_args(struct string_list *trailers)
{
	struct trailer_item *arg_tok_first = NULL;
	struct trailer_item *arg_tok_last = NULL;
	struct string_list_item *tr;

	for_each_string_list_item(tr, trailers) {
		struct trailer_item *new = create_trailer_item(tr->string);
		add_trailer_item(&arg_tok_first, &arg_tok_last, new);
	}

	return arg_tok_first;
}
