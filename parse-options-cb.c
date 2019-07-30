#include "git-compat-util.h"
#include "parse-options.h"
#include "cache.h"
#include "commit.h"
#include "color.h"
#include "string-list.h"
#include "argv-array.h"
#include "sha1-array.h"

/*----- some often used options -----*/

int parse_opt_abbrev_cb(const struct option *opt, const char *arg, int unset)
{
	int v;

	if (!arg) {
		v = unset ? 0 : DEFAULT_ABBREV;
	} else {
		if (!*arg)
			return error(_("option `%s' expects a numerical value"),
				     opt->long_name);
		v = strtol(arg, (char **)&arg, 10);
		if (*arg)
			return error(_("option `%s' expects a numerical value"),
				     opt->long_name);
		if (v && v < MINIMUM_ABBREV)
			v = MINIMUM_ABBREV;
		else if (v > the_hash_algo->hexsz)
			v = the_hash_algo->hexsz;
	}
	*(int *)(opt->value) = v;
	return 0;
}

int parse_opt_expiry_date_cb(const struct option *opt, const char *arg,
			     int unset)
{
	if (unset)
		arg = "never";
	if (parse_expiry_date(arg, (timestamp_t *)opt->value))
		die(_("malformed expiration date '%s'"), arg);
	return 0;
}

int parse_opt_color_flag_cb(const struct option *opt, const char *arg,
			    int unset)
{
	int value;

	if (!arg)
		arg = unset ? "never" : (const char *)opt->defval;
	value = git_config_colorbool(NULL, arg);
	if (value < 0)
		return error(_("option `%s' expects \"always\", \"auto\", or \"never\""),
			     opt->long_name);
	*(int *)opt->value = value;
	return 0;
}

int parse_opt_verbosity_cb(const struct option *opt, const char *arg,
			   int unset)
{
	int *target = opt->value;

	BUG_ON_OPT_ARG(arg);

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

int parse_opt_commits(const struct option *opt, const char *arg, int unset)
{
	struct object_id oid;
	struct commit *commit;

	BUG_ON_OPT_NEG(unset);

	if (!arg)
		return -1;
	if (get_oid(arg, &oid))
		return error("malformed object name %s", arg);
	commit = lookup_commit_reference(the_repository, &oid);
	if (!commit)
		return error("no such commit %s", arg);
	commit_list_insert(commit, opt->value);
	return 0;
}

int parse_opt_commit(const struct option *opt, const char *arg, int unset)
{
	struct object_id oid;
	struct commit *commit;
	struct commit **target = opt->value;

	if (!arg)
		return -1;
	if (get_oid(arg, &oid))
		return error("malformed object name %s", arg);
	commit = lookup_commit_reference(the_repository, &oid);
	if (!commit)
		return error("no such commit %s", arg);
	*target = commit;
	return 0;
}

int parse_opt_object_name(const struct option *opt, const char *arg, int unset)
{
	struct object_id oid;

	if (unset) {
		oid_array_clear(opt->value);
		return 0;
	}
	if (!arg)
		return -1;
	if (get_oid(arg, &oid))
		return error(_("malformed object name '%s'"), arg);
	oid_array_append(opt->value, &oid);
	return 0;
}

int parse_opt_object_id(const struct option *opt, const char *arg, int unset)
{
	struct object_id oid;
	struct object_id *target = opt->value;

	if (unset) {
		*target = null_oid;
		return 0;
	}
	if (!arg)
		return -1;
	if (get_oid(arg, &oid))
		return error(_("malformed object name '%s'"), arg);
	*target = oid;
	return 0;
}

int parse_opt_tertiary(const struct option *opt, const char *arg, int unset)
{
	int *target = opt->value;

	BUG_ON_OPT_ARG(arg);

	*target = unset ? 2 : 1;
	return 0;
}

struct option *parse_options_dup(const struct option *o)
{
	struct option *opts;
	int nr = 0;

	while (o && o->type != OPTION_END) {
		nr++;
		o++;
	}

	ALLOC_ARRAY(opts, nr + 1);
	memcpy(opts, o - nr, sizeof(*o) * nr);
	memset(opts + nr, 0, sizeof(*opts));
	opts[nr].type = OPTION_END;
	return opts;
}

struct option *parse_options_concat(struct option *a, struct option *b)
{
	struct option *ret;
	size_t i, a_len = 0, b_len = 0;

	for (i = 0; a[i].type != OPTION_END; i++)
		a_len++;
	for (i = 0; b[i].type != OPTION_END; i++)
		b_len++;

	ALLOC_ARRAY(ret, st_add3(a_len, b_len, 1));
	for (i = 0; i < a_len; i++)
		ret[i] = a[i];
	for (i = 0; i < b_len; i++)
		ret[a_len + i] = b[i];
	ret[a_len + b_len] = b[b_len]; /* final OPTION_END */

	return ret;
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

	string_list_append(v, arg);
	return 0;
}

int parse_opt_noop_cb(const struct option *opt, const char *arg, int unset)
{
	return 0;
}

/**
 * Report that the option is unknown, so that other code can handle
 * it. This can be used as a callback together with
 * OPTION_LOWLEVEL_CALLBACK to allow an option to be documented in the
 * "-h" output even if it's not being handled directly by
 * parse_options().
 */
enum parse_opt_result parse_opt_unknown_cb(struct parse_opt_ctx_t *ctx,
					   const struct option *opt,
					   const char *arg, int unset)
{
	BUG_ON_OPT_ARG(arg);
	return PARSE_OPT_UNKNOWN;
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
