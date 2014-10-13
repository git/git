#include "cache.h"
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
