#include "git-compat-util.h"
#include "parse-options.h"
#include "cache.h"
#include "commit.h"
#include "color.h"
#include "string-list.h"
#include "argv-array.h"

/*----- some often used options -----*/

int parse_opt_abbrev_cb(const struct option *opt, const char *arg, int unset)
{
	int v;

	if (!arg) {
		v = unset ? 0 : DEFAULT_ABBREV;
	} else {
		v = strtol(arg, (char **)&arg, 10);
		if (*arg)
			return opterror(opt, "expects a numerical value", 0);
		if (v && v < MINIMUM_ABBREV)
			v = MINIMUM_ABBREV;
		else if (v > 40)
			v = 40;
	}
	*(int *)(opt->value) = v;
	return 0;
}

int parse_opt_approxidate_cb(const struct option *opt, const char *arg,
			     int unset)
{
	*(unsigned long *)(opt->value) = approxidate(arg);
	return 0;
}

int parse_opt_expiry_date_cb(const struct option *opt, const char *arg,
			     int unset)
{
	return parse_expiry_date(arg, (unsigned long *)opt->value);
}

int parse_opt_color_flag_cb(const struct option *opt, const char *arg,
			    int unset)
{
	int value;

	if (!arg)
		arg = unset ? "never" : (const char *)opt->defval;
	value = git_config_colorbool(NULL, arg);
	if (value < 0)
		return opterror(opt,
			"expects \"always\", \"auto\", or \"never\"", 0);
	*(int *)opt->value = value;
	return 0;
}

int parse_opt_verbosity_cb(const struct option *opt, const char *arg,
			   int unset)
{
	int *target = opt->value;

	if (unset)
		/* --no-quiet, --no-verbose */
		*target = 0;
	else if (opt->short_name == 'v') {
		if (*target >= 0)
			(*target)++;
		else
			*target = 1;
	} else {
		if (*target <= 0)
			(*target)--;
		else
			*target = -1;
	}
	return 0;
}

int parse_opt_with_commit(const struct option *opt, const char *arg, int unset)
{
	unsigned char sha1[20];
	struct commit *commit;

	if (!arg)
		return -1;
	if (get_sha1(arg, sha1))
		return error("malformed object name %s", arg);
	commit = lookup_commit_reference(sha1);
	if (!commit)
		return error("no such commit %s", arg);
	commit_list_insert(commit, opt->value);
	return 0;
}

int parse_opt_tertiary(const struct option *opt, const char *arg, int unset)
{
	int *target = opt->value;
	*target = unset ? 2 : 1;
	return 0;
}

int parse_options_concat(struct option *dst, size_t dst_size, struct option *src)
{
	int i, j;

	for (i = 0; i < dst_size; i++)
		if (dst[i].type == OPTION_END)
			break;
	for (j = 0; i < dst_size; i++, j++) {
		dst[i] = src[j];
		if (src[j].type == OPTION_END)
			return 0;
	}
	return -1;
}

int parse_opt_string_list(const struct option *opt, const char *arg, int unset)
{
	struct string_list *v = opt->value;

	if (unset) {
		string_list_clear(v, 0);
		return 0;
	}

	if (!arg)
		return -1;

	string_list_append(v, xstrdup(arg));
	return 0;
}

int parse_opt_noop_cb(const struct option *opt, const char *arg, int unset)
{
	return 0;
}

/**
 * Recreates the command-line option in the strbuf.
 */
static int recreate_opt(struct strbuf *sb, const struct option *opt,
		const char *arg, int unset)
{
	strbuf_reset(sb);

	if (opt->long_name) {
		strbuf_addstr(sb, unset ? "--no-" : "--");
		strbuf_addstr(sb, opt->long_name);
		if (arg) {
			strbuf_addch(sb, '=');
			strbuf_addstr(sb, arg);
		}
	} else if (opt->short_name && !unset) {
		strbuf_addch(sb, '-');
		strbuf_addch(sb, opt->short_name);
		if (arg)
			strbuf_addstr(sb, arg);
	} else
		return -1;

	return 0;
}

/**
 * For an option opt, recreates the command-line option in opt->value which
 * must be an char* initialized to NULL. This is useful when we need to pass
 * the command-line option to another command. Since any previous value will be
 * overwritten, this callback should only be used for options where the last
 * one wins.
 */
int parse_opt_passthru(const struct option *opt, const char *arg, int unset)
{
	static struct strbuf sb = STRBUF_INIT;
	char **opt_value = opt->value;

	if (recreate_opt(&sb, opt, arg, unset) < 0)
		return -1;

	if (*opt_value)
		free(*opt_value);

	*opt_value = strbuf_detach(&sb, NULL);

	return 0;
}

/**
 * For an option opt, recreate the command-line option, appending it to
 * opt->value which must be a argv_array. This is useful when we need to pass
 * the command-line option, which can be specified multiple times, to another
 * command.
 */
int parse_opt_passthru_argv(const struct option *opt, const char *arg, int unset)
{
	static struct strbuf sb = STRBUF_INIT;
	struct argv_array *opt_value = opt->value;

	if (recreate_opt(&sb, opt, arg, unset) < 0)
		return -1;

	argv_array_push(opt_value, sb.buf);

	return 0;
}
