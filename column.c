#include "cache.h"
#include "column.h"
#include "string-list.h"
#include "parse-options.h"

/* Display without layout when not enabled */
static void display_plain(const struct string_list *list,
			  const char *indent, const char *nl)
{
	int i;

	for (i = 0; i < list->nr; i++)
		printf("%s%s%s", indent, list->items[i].string, nl);
}

void print_columns(const struct string_list *list, unsigned int colopts,
		   const struct column_options *opts)
{
	struct column_options nopts;

	if (!list->nr)
		return;
	assert((colopts & COL_ENABLE_MASK) != COL_AUTO);

	memset(&nopts, 0, sizeof(nopts));
	nopts.indent = opts && opts->indent ? opts->indent : "";
	nopts.nl = opts && opts->nl ? opts->nl : "\n";
	nopts.padding = opts ? opts->padding : 1;
	nopts.width = opts && opts->width ? opts->width : term_columns() - 1;
	if (!column_active(colopts)) {
		display_plain(list, "", "\n");
		return;
	}
	switch (COL_LAYOUT(colopts)) {
	case COL_PLAIN:
		display_plain(list, nopts.indent, nopts.nl);
		break;
	default:
		die("BUG: invalid layout mode %d", COL_LAYOUT(colopts));
	}
}

int finalize_colopts(unsigned int *colopts, int stdout_is_tty)
{
	if ((*colopts & COL_ENABLE_MASK) == COL_AUTO) {
		if (stdout_is_tty < 0)
			stdout_is_tty = isatty(1);
		*colopts &= ~COL_ENABLE_MASK;
		if (stdout_is_tty)
			*colopts |= COL_ENABLED;
	}
	return 0;
}

struct colopt {
	const char *name;
	unsigned int value;
	unsigned int mask;
};

#define LAYOUT_SET 1
#define ENABLE_SET 2

static int parse_option(const char *arg, int len, unsigned int *colopts,
			int *group_set)
{
	struct colopt opts[] = {
		{ "always", COL_ENABLED,  COL_ENABLE_MASK },
		{ "never",  COL_DISABLED, COL_ENABLE_MASK },
		{ "auto",   COL_AUTO,     COL_ENABLE_MASK },
		{ "plain",  COL_PLAIN,    COL_LAYOUT_MASK },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(opts); i++) {
		int arg_len = len, name_len;
		const char *arg_str = arg;

		name_len = strlen(opts[i].name);
		if (arg_len != name_len ||
		    strncmp(arg_str, opts[i].name, name_len))
			continue;

		switch (opts[i].mask) {
		case COL_ENABLE_MASK:
			*group_set |= ENABLE_SET;
			break;
		case COL_LAYOUT_MASK:
			*group_set |= LAYOUT_SET;
			break;
		}

		if (opts[i].mask)
			*colopts = (*colopts & ~opts[i].mask) | opts[i].value;
		return 0;
	}

	return error("unsupported option '%s'", arg);
}

static int parse_config(unsigned int *colopts, const char *value)
{
	const char *sep = " ,";
	int group_set = 0;

	while (*value) {
		int len = strcspn(value, sep);
		if (len) {
			if (parse_option(value, len, colopts, &group_set))
				return -1;

			value += len;
		}
		value += strspn(value, sep);
	}
	/*
	 * Setting layout implies "always" if neither always, never
	 * nor auto is specified.
	 *
	 * Current value in COL_ENABLE_MASK is disregarded. This means if
	 * you set column.ui = auto and pass --column=row, then "auto"
	 * will become "always".
	 */
	if ((group_set & LAYOUT_SET) && !(group_set & ENABLE_SET))
		*colopts = (*colopts & ~COL_ENABLE_MASK) | COL_ENABLED;
	return 0;
}

static int column_config(const char *var, const char *value,
			 const char *key, unsigned int *colopts)
{
	if (!value)
		return config_error_nonbool(var);
	if (parse_config(colopts, value))
		return error("invalid column.%s mode %s", key, value);
	return 0;
}

int git_column_config(const char *var, const char *value,
		      const char *command, unsigned int *colopts)
{
	const char *it = skip_prefix(var, "column.");
	if (!it)
		return 0;

	if (!strcmp(it, "ui"))
		return column_config(var, value, "ui", colopts);

	if (command && !strcmp(it, command))
		return column_config(var, value, it, colopts);

	return 0;
}

int parseopt_column_callback(const struct option *opt,
			     const char *arg, int unset)
{
	unsigned int *colopts = opt->value;
	*colopts |= COL_PARSEOPT;
	*colopts &= ~COL_ENABLE_MASK;
	if (unset)		/* --no-column == never */
		return 0;
	/* --column == always unless "arg" states otherwise */
	*colopts |= COL_ENABLED;
	if (arg)
		return parse_config(colopts, arg);

	return 0;
}
