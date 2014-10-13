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
