/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Johannes Schindelin, 2005
 *
 */
#include "cache.h"
#include "date.h"
#include "branch.h"
#include "config.h"
#include "environment.h"
#include "repository.h"
#include "lockfile.h"
#include "exec-cmd.h"
#include "strbuf.h"
#include "quote.h"
#include "hashmap.h"
#include "string-list.h"
#include "object-store.h"
#include "utf8.h"
#include "dir.h"
#include "color.h"
#include "refs.h"
#include "worktree.h"

struct config_source {
	struct config_source *prev;
	union {
		FILE *file;
		struct config_buf {
			const char *buf;
			size_t len;
			size_t pos;
		} buf;
	} u;
	enum config_origin_type origin_type;
	const char *name;
	const char *path;
	enum config_error_action default_error_action;
	int linenr;
	int eof;
	size_t total_len;
	struct strbuf value;
	struct strbuf var;
	unsigned subsection_case_sensitive : 1;

	int (*do_fgetc)(struct config_source *c);
	int (*do_ungetc)(int c, struct config_source *conf);
	long (*do_ftell)(struct config_source *c);
};

/*
 * These variables record the "current" config source, which
 * can be accessed by parsing callbacks.
 *
 * The "cf" variable will be non-NULL only when we are actually parsing a real
 * config source (file, blob, cmdline, etc).
 *
 * The "current_config_kvi" variable will be non-NULL only when we are feeding
 * cached config from a configset into a callback.
 *
 * They should generally never be non-NULL at the same time. If they are both
 * NULL, then we aren't parsing anything (and depending on the function looking
 * at the variables, it's either a bug for it to be called in the first place,
 * or it's a function which can be reused for non-config purposes, and should
 * fall back to some sane behavior).
 */
static struct config_source *cf;
static struct key_value_info *current_config_kvi;

/*
 * Similar to the variables above, this gives access to the "scope" of the
 * current value (repo, global, etc). For cached values, it can be found via
 * the current_config_kvi as above. During parsing, the current value can be
 * found in this variable. It's not part of "cf" because it transcends a single
 * file (i.e., a file included from .git/config is still in "repo" scope).
 */
static enum config_scope current_parsing_scope;

static int pack_compression_seen;
static int zlib_compression_seen;

static int config_file_fgetc(struct config_source *conf)
{
	return getc_unlocked(conf->u.file);
}

static int config_file_ungetc(int c, struct config_source *conf)
{
	return ungetc(c, conf->u.file);
}

static long config_file_ftell(struct config_source *conf)
{
	return ftell(conf->u.file);
}


static int config_buf_fgetc(struct config_source *conf)
{
	if (conf->u.buf.pos < conf->u.buf.len)
		return conf->u.buf.buf[conf->u.buf.pos++];

	return EOF;
}

static int config_buf_ungetc(int c, struct config_source *conf)
{
	if (conf->u.buf.pos > 0) {
		conf->u.buf.pos--;
		if (conf->u.buf.buf[conf->u.buf.pos] != c)
			BUG("config_buf can only ungetc the same character");
		return c;
	}

	return EOF;
}

static long config_buf_ftell(struct config_source *conf)
{
	return conf->u.buf.pos;
}

struct config_include_data {
	int depth;
	config_fn_t fn;
	void *data;
	const struct config_options *opts;
	struct git_config_source *config_source;

	/*
	 * All remote URLs discovered when reading all config files.
	 */
	struct string_list *remote_urls;
};
#define CONFIG_INCLUDE_INIT { 0 }

static int git_config_include(const char *var, const char *value, void *data);

#define MAX_INCLUDE_DEPTH 10
static const char include_depth_advice[] = N_(
"exceeded maximum include depth (%d) while including\n"
"	%s\n"
"from\n"
"	%s\n"
"This might be due to circular includes.");
static int handle_path_include(const char *path, struct config_include_data *inc)
{
	int ret = 0;
	struct strbuf buf = STRBUF_INIT;
	char *expanded;

	if (!path)
		return config_error_nonbool("include.path");

	expanded = interpolate_path(path, 0);
	if (!expanded)
		return error(_("could not expand include path '%s'"), path);
	path = expanded;

	/*
	 * Use an absolute path as-is, but interpret relative paths
	 * based on the including config file.
	 */
	if (!is_absolute_path(path)) {
		char *slash;

		if (!cf || !cf->path) {
			ret = error(_("relative config includes must come from files"));
			goto cleanup;
		}

		slash = find_last_dir_sep(cf->path);
		if (slash)
			strbuf_add(&buf, cf->path, slash - cf->path + 1);
		strbuf_addstr(&buf, path);
		path = buf.buf;
	}

	if (!access_or_die(path, R_OK, 0)) {
		if (++inc->depth > MAX_INCLUDE_DEPTH)
			die(_(include_depth_advice), MAX_INCLUDE_DEPTH, path,
			    !cf ? "<unknown>" :
			    cf->name ? cf->name :
			    "the command line");
		ret = git_config_from_file(git_config_include, path, inc);
		inc->depth--;
	}
cleanup:
	strbuf_release(&buf);
	free(expanded);
	return ret;
}

static void add_trailing_starstar_for_dir(struct strbuf *pat)
{
	if (pat->len && is_dir_sep(pat->buf[pat->len - 1]))
		strbuf_addstr(pat, "**");
}

static int prepare_include_condition_pattern(struct strbuf *pat)
{
	struct strbuf path = STRBUF_INIT;
	char *expanded;
	int prefix = 0;

	expanded = interpolate_path(pat->buf, 1);
	if (expanded) {
		strbuf_reset(pat);
		strbuf_addstr(pat, expanded);
		free(expanded);
	}

	if (pat->buf[0] == '.' && is_dir_sep(pat->buf[1])) {
		const char *slash;

		if (!cf || !cf->path)
			return error(_("relative config include "
				       "conditionals must come from files"));

		strbuf_realpath(&path, cf->path, 1);
		slash = find_last_dir_sep(path.buf);
		if (!slash)
			BUG("how is this possible?");
		strbuf_splice(pat, 0, 1, path.buf, slash - path.buf);
		prefix = slash - path.buf + 1 /* slash */;
	} else if (!is_absolute_path(pat->buf))
		strbuf_insertstr(pat, 0, "**/");

	add_trailing_starstar_for_dir(pat);

	strbuf_release(&path);
	return prefix;
}

static int include_by_gitdir(const struct config_options *opts,
			     const char *cond, size_t cond_len, int icase)
{
	struct strbuf text = STRBUF_INIT;
	struct strbuf pattern = STRBUF_INIT;
	int ret = 0, prefix;
	const char *git_dir;
	int already_tried_absolute = 0;

	if (opts->git_dir)
		git_dir = opts->git_dir;
	else
		goto done;

	strbuf_realpath(&text, git_dir, 1);
	strbuf_add(&pattern, cond, cond_len);
	prefix = prepare_include_condition_pattern(&pattern);

again:
	if (prefix < 0)
		goto done;

	if (prefix > 0) {
		/*
		 * perform literal matching on the prefix part so that
		 * any wildcard character in it can't create side effects.
		 */
		if (text.len < prefix)
			goto done;
		if (!icase && strncmp(pattern.buf, text.buf, prefix))
			goto done;
		if (icase && strncasecmp(pattern.buf, text.buf, prefix))
			goto done;
	}

	ret = !wildmatch(pattern.buf + prefix, text.buf + prefix,
			 WM_PATHNAME | (icase ? WM_CASEFOLD : 0));

	if (!ret && !already_tried_absolute) {
		/*
		 * We've tried e.g. matching gitdir:~/work, but if
		 * ~/work is a symlink to /mnt/storage/work
		 * strbuf_realpath() will expand it, so the rule won't
		 * match. Let's match against a
		 * strbuf_add_absolute_path() version of the path,
		 * which'll do the right thing
		 */
		strbuf_reset(&text);
		strbuf_add_absolute_path(&text, git_dir);
		already_tried_absolute = 1;
		goto again;
	}
done:
	strbuf_release(&pattern);
	strbuf_release(&text);
	return ret;
}

static int include_by_branch(const char *cond, size_t cond_len)
{
	int flags;
	int ret;
	struct strbuf pattern = STRBUF_INIT;
	const char *refname = !the_repository->gitdir ?
		NULL : resolve_ref_unsafe("HEAD", 0, NULL, &flags);
	const char *shortname;

	if (!refname || !(flags & REF_ISSYMREF)	||
			!skip_prefix(refname, "refs/heads/", &shortname))
		return 0;

	strbuf_add(&pattern, cond, cond_len);
	add_trailing_starstar_for_dir(&pattern);
	ret = !wildmatch(pattern.buf, shortname, WM_PATHNAME);
	strbuf_release(&pattern);
	return ret;
}

static int add_remote_url(const char *var, const char *value, void *data)
{
	struct string_list *remote_urls = data;
	const char *remote_name;
	size_t remote_name_len;
	const char *key;

	if (!parse_config_key(var, "remote", &remote_name, &remote_name_len,
			      &key) &&
	    remote_name &&
	    !strcmp(key, "url"))
		string_list_append(remote_urls, value);
	return 0;
}

static void populate_remote_urls(struct config_include_data *inc)
{
	struct config_options opts;

	struct config_source *store_cf = cf;
	struct key_value_info *store_kvi = current_config_kvi;
	enum config_scope store_scope = current_parsing_scope;

	opts = *inc->opts;
	opts.unconditional_remote_url = 1;

	cf = NULL;
	current_config_kvi = NULL;
	current_parsing_scope = 0;

	inc->remote_urls = xmalloc(sizeof(*inc->remote_urls));
	string_list_init_dup(inc->remote_urls);
	config_with_options(add_remote_url, inc->remote_urls, inc->config_source, &opts);

	cf = store_cf;
	current_config_kvi = store_kvi;
	current_parsing_scope = store_scope;
}

static int forbid_remote_url(const char *var, const char *value, void *data)
{
	const char *remote_name;
	size_t remote_name_len;
	const char *key;

	if (!parse_config_key(var, "remote", &remote_name, &remote_name_len,
			      &key) &&
	    remote_name &&
	    !strcmp(key, "url"))
		die(_("remote URLs cannot be configured in file directly or indirectly included by includeIf.hasconfig:remote.*.url"));
	return 0;
}

static int at_least_one_url_matches_glob(const char *glob, int glob_len,
					 struct string_list *remote_urls)
{
	struct strbuf pattern = STRBUF_INIT;
	struct string_list_item *url_item;
	int found = 0;

	strbuf_add(&pattern, glob, glob_len);
	for_each_string_list_item(url_item, remote_urls) {
		if (!wildmatch(pattern.buf, url_item->string, WM_PATHNAME)) {
			found = 1;
			break;
		}
	}
	strbuf_release(&pattern);
	return found;
}

static int include_by_remote_url(struct config_include_data *inc,
		const char *cond, size_t cond_len)
{
	if (inc->opts->unconditional_remote_url)
		return 1;
	if (!inc->remote_urls)
		populate_remote_urls(inc);
	return at_least_one_url_matches_glob(cond, cond_len,
					     inc->remote_urls);
}

static int include_condition_is_true(struct config_include_data *inc,
				     const char *cond, size_t cond_len)
{
	const struct config_options *opts = inc->opts;

	if (skip_prefix_mem(cond, cond_len, "gitdir:", &cond, &cond_len))
		return include_by_gitdir(opts, cond, cond_len, 0);
	else if (skip_prefix_mem(cond, cond_len, "gitdir/i:", &cond, &cond_len))
		return include_by_gitdir(opts, cond, cond_len, 1);
	else if (skip_prefix_mem(cond, cond_len, "onbranch:", &cond, &cond_len))
		return include_by_branch(cond, cond_len);
	else if (skip_prefix_mem(cond, cond_len, "hasconfig:remote.*.url:", &cond,
				   &cond_len))
		return include_by_remote_url(inc, cond, cond_len);

	/* unknown conditionals are always false */
	return 0;
}

static int git_config_include(const char *var, const char *value, void *data)
{
	struct config_include_data *inc = data;
	const char *cond, *key;
	size_t cond_len;
	int ret;

	/*
	 * Pass along all values, including "include" directives; this makes it
	 * possible to query information on the includes themselves.
	 */
	ret = inc->fn(var, value, inc->data);
	if (ret < 0)
		return ret;

	if (!strcmp(var, "include.path"))
		ret = handle_path_include(value, inc);

	if (!parse_config_key(var, "includeif", &cond, &cond_len, &key) &&
	    cond && include_condition_is_true(inc, cond, cond_len) &&
	    !strcmp(key, "path")) {
		config_fn_t old_fn = inc->fn;

		if (inc->opts->unconditional_remote_url)
			inc->fn = forbid_remote_url;
		ret = handle_path_include(value, inc);
		inc->fn = old_fn;
	}

	return ret;
}

static void git_config_push_split_parameter(const char *key, const char *value)
{
	struct strbuf env = STRBUF_INIT;
	const char *old = getenv(CONFIG_DATA_ENVIRONMENT);
	if (old && *old) {
		strbuf_addstr(&env, old);
		strbuf_addch(&env, ' ');
	}
	sq_quote_buf(&env, key);
	strbuf_addch(&env, '=');
	if (value)
		sq_quote_buf(&env, value);
	setenv(CONFIG_DATA_ENVIRONMENT, env.buf, 1);
	strbuf_release(&env);
}

void git_config_push_parameter(const char *text)
{
	const char *value;

	/*
	 * When we see:
	 *
	 *   section.subsection=with=equals.key=value
	 *
	 * we cannot tell if it means:
	 *
	 *   [section "subsection=with=equals"]
	 *   key = value
	 *
	 * or:
	 *
	 *   [section]
	 *   subsection = with=equals.key=value
	 *
	 * We parse left-to-right for the first "=", meaning we'll prefer to
	 * keep the value intact over the subsection. This is historical, but
	 * also sensible since values are more likely to contain odd or
	 * untrusted input than a section name.
	 *
	 * A missing equals is explicitly allowed (as a bool-only entry).
	 */
	value = strchr(text, '=');
	if (value) {
		char *key = xmemdupz(text, value - text);
		git_config_push_split_parameter(key, value + 1);
		free(key);
	} else {
		git_config_push_split_parameter(text, NULL);
	}
}

void git_config_push_env(const char *spec)
{
	char *key;
	const char *env_name;
	const char *env_value;

	env_name = strrchr(spec, '=');
	if (!env_name)
		die(_("invalid config format: %s"), spec);
	key = xmemdupz(spec, env_name - spec);
	env_name++;
	if (!*env_name)
		die(_("missing environment variable name for configuration '%.*s'"),
		    (int)(env_name - spec - 1), spec);

	env_value = getenv(env_name);
	if (!env_value)
		die(_("missing environment variable '%s' for configuration '%.*s'"),
		    env_name, (int)(env_name - spec - 1), spec);

	git_config_push_split_parameter(key, env_value);
	free(key);
}

static inline int iskeychar(int c)
{
	return isalnum(c) || c == '-';
}

/*
 * Auxiliary function to sanity-check and split the key into the section
 * identifier and variable name.
 *
 * Returns 0 on success, -1 when there is an invalid character in the key and
 * -2 if there is no section name in the key.
 *
 * store_key - pointer to char* which will hold a copy of the key with
 *             lowercase section and variable name
 * baselen - pointer to size_t which will hold the length of the
 *           section + subsection part, can be NULL
 */
int git_config_parse_key(const char *key, char **store_key, size_t *baselen_)
{
	size_t i, baselen;
	int dot;
	const char *last_dot = strrchr(key, '.');

	/*
	 * Since "key" actually contains the section name and the real
	 * key name separated by a dot, we have to know where the dot is.
	 */

	if (last_dot == NULL || last_dot == key) {
		error(_("key does not contain a section: %s"), key);
		return -CONFIG_NO_SECTION_OR_NAME;
	}

	if (!last_dot[1]) {
		error(_("key does not contain variable name: %s"), key);
		return -CONFIG_NO_SECTION_OR_NAME;
	}

	baselen = last_dot - key;
	if (baselen_)
		*baselen_ = baselen;

	/*
	 * Validate the key and while at it, lower case it for matching.
	 */
	*store_key = xmallocz(strlen(key));

	dot = 0;
	for (i = 0; key[i]; i++) {
		unsigned char c = key[i];
		if (c == '.')
			dot = 1;
		/* Leave the extended basename untouched.. */
		if (!dot || i > baselen) {
			if (!iskeychar(c) ||
			    (i == baselen + 1 && !isalpha(c))) {
				error(_("invalid key: %s"), key);
				goto out_free_ret_1;
			}
			c = tolower(c);
		} else if (c == '\n') {
			error(_("invalid key (newline): %s"), key);
			goto out_free_ret_1;
		}
		(*store_key)[i] = c;
	}

	return 0;

out_free_ret_1:
	FREE_AND_NULL(*store_key);
	return -CONFIG_INVALID_KEY;
}

static int config_parse_pair(const char *key, const char *value,
			  config_fn_t fn, void *data)
{
	char *canonical_name;
	int ret;

	if (!strlen(key))
		return error(_("empty config key"));
	if (git_config_parse_key(key, &canonical_name, NULL))
		return -1;

	ret = (fn(canonical_name, value, data) < 0) ? -1 : 0;
	free(canonical_name);
	return ret;
}

int git_config_parse_parameter(const char *text,
			       config_fn_t fn, void *data)
{
	const char *value;
	struct strbuf **pair;
	int ret;

	pair = strbuf_split_str(text, '=', 2);
	if (!pair[0])
		return error(_("bogus config parameter: %s"), text);

	if (pair[0]->len && pair[0]->buf[pair[0]->len - 1] == '=') {
		strbuf_setlen(pair[0], pair[0]->len - 1);
		value = pair[1] ? pair[1]->buf : "";
	} else {
		value = NULL;
	}

	strbuf_trim(pair[0]);
	if (!pair[0]->len) {
		strbuf_list_free(pair);
		return error(_("bogus config parameter: %s"), text);
	}

	ret = config_parse_pair(pair[0]->buf, value, fn, data);
	strbuf_list_free(pair);
	return ret;
}

static int parse_config_env_list(char *env, config_fn_t fn, void *data)
{
	char *cur = env;
	while (cur && *cur) {
		const char *key = sq_dequote_step(cur, &cur);
		if (!key)
			return error(_("bogus format in %s"),
				     CONFIG_DATA_ENVIRONMENT);

		if (!cur || isspace(*cur)) {
			/* old-style 'key=value' */
			if (git_config_parse_parameter(key, fn, data) < 0)
				return -1;
		}
		else if (*cur == '=') {
			/* new-style 'key'='value' */
			const char *value;

			cur++;
			if (*cur == '\'') {
				/* quoted value */
				value = sq_dequote_step(cur, &cur);
				if (!value || (cur && !isspace(*cur))) {
					return error(_("bogus format in %s"),
						     CONFIG_DATA_ENVIRONMENT);
				}
			} else if (!*cur || isspace(*cur)) {
				/* implicit bool: 'key'= */
				value = NULL;
			} else {
				return error(_("bogus format in %s"),
					     CONFIG_DATA_ENVIRONMENT);
			}

			if (config_parse_pair(key, value, fn, data) < 0)
				return -1;
		}
		else {
			/* unknown format */
			return error(_("bogus format in %s"),
				     CONFIG_DATA_ENVIRONMENT);
		}

		if (cur) {
			while (isspace(*cur))
				cur++;
		}
	}
	return 0;
}

int git_config_from_parameters(config_fn_t fn, void *data)
{
	const char *env;
	struct strbuf envvar = STRBUF_INIT;
	struct strvec to_free = STRVEC_INIT;
	int ret = 0;
	char *envw = NULL;
	struct config_source source;

	memset(&source, 0, sizeof(source));
	source.prev = cf;
	source.origin_type = CONFIG_ORIGIN_CMDLINE;
	cf = &source;

	env = getenv(CONFIG_COUNT_ENVIRONMENT);
	if (env) {
		unsigned long count;
		char *endp;
		int i;

		count = strtoul(env, &endp, 10);
		if (*endp) {
			ret = error(_("bogus count in %s"), CONFIG_COUNT_ENVIRONMENT);
			goto out;
		}
		if (count > INT_MAX) {
			ret = error(_("too many entries in %s"), CONFIG_COUNT_ENVIRONMENT);
			goto out;
		}

		for (i = 0; i < count; i++) {
			const char *key, *value;

			strbuf_addf(&envvar, "GIT_CONFIG_KEY_%d", i);
			key = getenv_safe(&to_free, envvar.buf);
			if (!key) {
				ret = error(_("missing config key %s"), envvar.buf);
				goto out;
			}
			strbuf_reset(&envvar);

			strbuf_addf(&envvar, "GIT_CONFIG_VALUE_%d", i);
			value = getenv_safe(&to_free, envvar.buf);
			if (!value) {
				ret = error(_("missing config value %s"), envvar.buf);
				goto out;
			}
			strbuf_reset(&envvar);

			if (config_parse_pair(key, value, fn, data) < 0) {
				ret = -1;
				goto out;
			}
		}
	}

	env = getenv(CONFIG_DATA_ENVIRONMENT);
	if (env) {
		/* sq_dequote will write over it */
		envw = xstrdup(env);
		if (parse_config_env_list(envw, fn, data) < 0) {
			ret = -1;
			goto out;
		}
	}

out:
	strbuf_release(&envvar);
	strvec_clear(&to_free);
	free(envw);
	cf = source.prev;
	return ret;
}

static int get_next_char(void)
{
	int c = cf->do_fgetc(cf);

	if (c == '\r') {
		/* DOS like systems */
		c = cf->do_fgetc(cf);
		if (c != '\n') {
			if (c != EOF)
				cf->do_ungetc(c, cf);
			c = '\r';
		}
	}

	if (c != EOF && ++cf->total_len > INT_MAX) {
		/*
		 * This is an absurdly long config file; refuse to parse
		 * further in order to protect downstream code from integer
		 * overflows. Note that we can't return an error specifically,
		 * but we can mark EOF and put trash in the return value,
		 * which will trigger a parse error.
		 */
		cf->eof = 1;
		return 0;
	}

	if (c == '\n')
		cf->linenr++;
	if (c == EOF) {
		cf->eof = 1;
		cf->linenr++;
		c = '\n';
	}
	return c;
}

static char *parse_value(void)
{
	int quote = 0, comment = 0, space = 0;

	strbuf_reset(&cf->value);
	for (;;) {
		int c = get_next_char();
		if (c == '\n') {
			if (quote) {
				cf->linenr--;
				return NULL;
			}
			return cf->value.buf;
		}
		if (comment)
			continue;
		if (isspace(c) && !quote) {
			if (cf->value.len)
				space++;
			continue;
		}
		if (!quote) {
			if (c == ';' || c == '#') {
				comment = 1;
				continue;
			}
		}
		for (; space; space--)
			strbuf_addch(&cf->value, ' ');
		if (c == '\\') {
			c = get_next_char();
			switch (c) {
			case '\n':
				continue;
			case 't':
				c = '\t';
				break;
			case 'b':
				c = '\b';
				break;
			case 'n':
				c = '\n';
				break;
			/* Some characters escape as themselves */
			case '\\': case '"':
				break;
			/* Reject unknown escape sequences */
			default:
				return NULL;
			}
			strbuf_addch(&cf->value, c);
			continue;
		}
		if (c == '"') {
			quote = 1-quote;
			continue;
		}
		strbuf_addch(&cf->value, c);
	}
}

static int get_value(config_fn_t fn, void *data, struct strbuf *name)
{
	int c;
	char *value;
	int ret;

	/* Get the full name */
	for (;;) {
		c = get_next_char();
		if (cf->eof)
			break;
		if (!iskeychar(c))
			break;
		strbuf_addch(name, tolower(c));
	}

	while (c == ' ' || c == '\t')
		c = get_next_char();

	value = NULL;
	if (c != '\n') {
		if (c != '=')
			return -1;
		value = parse_value();
		if (!value)
			return -1;
	}
	/*
	 * We already consumed the \n, but we need linenr to point to
	 * the line we just parsed during the call to fn to get
	 * accurate line number in error messages.
	 */
	cf->linenr--;
	ret = fn(name->buf, value, data);
	if (ret >= 0)
		cf->linenr++;
	return ret;
}

static int get_extended_base_var(struct strbuf *name, int c)
{
	cf->subsection_case_sensitive = 0;
	do {
		if (c == '\n')
			goto error_incomplete_line;
		c = get_next_char();
	} while (isspace(c));

	/* We require the format to be '[base "extension"]' */
	if (c != '"')
		return -1;
	strbuf_addch(name, '.');

	for (;;) {
		int c = get_next_char();
		if (c == '\n')
			goto error_incomplete_line;
		if (c == '"')
			break;
		if (c == '\\') {
			c = get_next_char();
			if (c == '\n')
				goto error_incomplete_line;
		}
		strbuf_addch(name, c);
	}

	/* Final ']' */
	if (get_next_char() != ']')
		return -1;
	return 0;
error_incomplete_line:
	cf->linenr--;
	return -1;
}

static int get_base_var(struct strbuf *name)
{
	cf->subsection_case_sensitive = 1;
	for (;;) {
		int c = get_next_char();
		if (cf->eof)
			return -1;
		if (c == ']')
			return 0;
		if (isspace(c))
			return get_extended_base_var(name, c);
		if (!iskeychar(c) && c != '.')
			return -1;
		strbuf_addch(name, tolower(c));
	}
}

struct parse_event_data {
	enum config_event_t previous_type;
	size_t previous_offset;
	const struct config_options *opts;
};

static int do_event(enum config_event_t type, struct parse_event_data *data)
{
	size_t offset;

	if (!data->opts || !data->opts->event_fn)
		return 0;

	if (type == CONFIG_EVENT_WHITESPACE &&
	    data->previous_type == type)
		return 0;

	offset = cf->do_ftell(cf);
	/*
	 * At EOF, the parser always "inserts" an extra '\n', therefore
	 * the end offset of the event is the current file position, otherwise
	 * we will already have advanced to the next event.
	 */
	if (type != CONFIG_EVENT_EOF)
		offset--;

	if (data->previous_type != CONFIG_EVENT_EOF &&
	    data->opts->event_fn(data->previous_type, data->previous_offset,
				 offset, data->opts->event_fn_data) < 0)
		return -1;

	data->previous_type = type;
	data->previous_offset = offset;

	return 0;
}

static int git_parse_source(config_fn_t fn, void *data,
			    const struct config_options *opts)
{
	int comment = 0;
	size_t baselen = 0;
	struct strbuf *var = &cf->var;
	int error_return = 0;
	char *error_msg = NULL;

	/* U+FEFF Byte Order Mark in UTF8 */
	const char *bomptr = utf8_bom;

	/* For the parser event callback */
	struct parse_event_data event_data = {
		CONFIG_EVENT_EOF, 0, opts
	};

	for (;;) {
		int c;

		c = get_next_char();
		if (bomptr && *bomptr) {
			/* We are at the file beginning; skip UTF8-encoded BOM
			 * if present. Sane editors won't put this in on their
			 * own, but e.g. Windows Notepad will do it happily. */
			if (c == (*bomptr & 0377)) {
				bomptr++;
				continue;
			} else {
				/* Do not tolerate partial BOM. */
				if (bomptr != utf8_bom)
					break;
				/* No BOM at file beginning. Cool. */
				bomptr = NULL;
			}
		}
		if (c == '\n') {
			if (cf->eof) {
				if (do_event(CONFIG_EVENT_EOF, &event_data) < 0)
					return -1;
				return 0;
			}
			if (do_event(CONFIG_EVENT_WHITESPACE, &event_data) < 0)
				return -1;
			comment = 0;
			continue;
		}
		if (comment)
			continue;
		if (isspace(c)) {
			if (do_event(CONFIG_EVENT_WHITESPACE, &event_data) < 0)
					return -1;
			continue;
		}
		if (c == '#' || c == ';') {
			if (do_event(CONFIG_EVENT_COMMENT, &event_data) < 0)
					return -1;
			comment = 1;
			continue;
		}
		if (c == '[') {
			if (do_event(CONFIG_EVENT_SECTION, &event_data) < 0)
					return -1;

			/* Reset prior to determining a new stem */
			strbuf_reset(var);
			if (get_base_var(var) < 0 || var->len < 1)
				break;
			strbuf_addch(var, '.');
			baselen = var->len;
			continue;
		}
		if (!isalpha(c))
			break;

		if (do_event(CONFIG_EVENT_ENTRY, &event_data) < 0)
			return -1;

		/*
		 * Truncate the var name back to the section header
		 * stem prior to grabbing the suffix part of the name
		 * and the value.
		 */
		strbuf_setlen(var, baselen);
		strbuf_addch(var, tolower(c));
		if (get_value(fn, data, var) < 0)
			break;
	}

	if (do_event(CONFIG_EVENT_ERROR, &event_data) < 0)
		return -1;

	switch (cf->origin_type) {
	case CONFIG_ORIGIN_BLOB:
		error_msg = xstrfmt(_("bad config line %d in blob %s"),
				      cf->linenr, cf->name);
		break;
	case CONFIG_ORIGIN_FILE:
		error_msg = xstrfmt(_("bad config line %d in file %s"),
				      cf->linenr, cf->name);
		break;
	case CONFIG_ORIGIN_STDIN:
		error_msg = xstrfmt(_("bad config line %d in standard input"),
				      cf->linenr);
		break;
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		error_msg = xstrfmt(_("bad config line %d in submodule-blob %s"),
				       cf->linenr, cf->name);
		break;
	case CONFIG_ORIGIN_CMDLINE:
		error_msg = xstrfmt(_("bad config line %d in command line %s"),
				       cf->linenr, cf->name);
		break;
	default:
		error_msg = xstrfmt(_("bad config line %d in %s"),
				      cf->linenr, cf->name);
	}

	switch (opts && opts->error_action ?
		opts->error_action :
		cf->default_error_action) {
	case CONFIG_ERROR_DIE:
		die("%s", error_msg);
		break;
	case CONFIG_ERROR_ERROR:
		error_return = error("%s", error_msg);
		break;
	case CONFIG_ERROR_SILENT:
		error_return = -1;
		break;
	case CONFIG_ERROR_UNSET:
		BUG("config error action unset");
	}

	free(error_msg);
	return error_return;
}

static uintmax_t get_unit_factor(const char *end)
{
	if (!*end)
		return 1;
	else if (!strcasecmp(end, "k"))
		return 1024;
	else if (!strcasecmp(end, "m"))
		return 1024 * 1024;
	else if (!strcasecmp(end, "g"))
		return 1024 * 1024 * 1024;
	return 0;
}

static int git_parse_signed(const char *value, intmax_t *ret, intmax_t max)
{
	if (value && *value) {
		char *end;
		intmax_t val;
		uintmax_t uval;
		uintmax_t factor;

		errno = 0;
		val = strtoimax(value, &end, 0);
		if (errno == ERANGE)
			return 0;
		factor = get_unit_factor(end);
		if (!factor) {
			errno = EINVAL;
			return 0;
		}
		uval = val < 0 ? -val : val;
		if (unsigned_mult_overflows(factor, uval) ||
		    factor * uval > max) {
			errno = ERANGE;
			return 0;
		}
		val *= factor;
		*ret = val;
		return 1;
	}
	errno = EINVAL;
	return 0;
}

static int git_parse_unsigned(const char *value, uintmax_t *ret, uintmax_t max)
{
	if (value && *value) {
		char *end;
		uintmax_t val;
		uintmax_t factor;

		errno = 0;
		val = strtoumax(value, &end, 0);
		if (errno == ERANGE)
			return 0;
		factor = get_unit_factor(end);
		if (!factor) {
			errno = EINVAL;
			return 0;
		}
		if (unsigned_mult_overflows(factor, val) ||
		    factor * val > max) {
			errno = ERANGE;
			return 0;
		}
		val *= factor;
		*ret = val;
		return 1;
	}
	errno = EINVAL;
	return 0;
}

static int git_parse_int(const char *value, int *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int)))
		return 0;
	*ret = tmp;
	return 1;
}

static int git_parse_int64(const char *value, int64_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int64_t)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_ulong(const char *value, unsigned long *ret)
{
	uintmax_t tmp;
	if (!git_parse_unsigned(value, &tmp, maximum_unsigned_value_of_type(long)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_ssize_t(const char *value, ssize_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(ssize_t)))
		return 0;
	*ret = tmp;
	return 1;
}

NORETURN
static void die_bad_number(const char *name, const char *value)
{
	const char *error_type = (errno == ERANGE) ?
		N_("out of range") : N_("invalid unit");
	const char *bad_numeric = N_("bad numeric config value '%s' for '%s': %s");

	if (!value)
		value = "";

	if (!(cf && cf->name))
		die(_(bad_numeric), value, name, _(error_type));

	switch (cf->origin_type) {
	case CONFIG_ORIGIN_BLOB:
		die(_("bad numeric config value '%s' for '%s' in blob %s: %s"),
		    value, name, cf->name, _(error_type));
	case CONFIG_ORIGIN_FILE:
		die(_("bad numeric config value '%s' for '%s' in file %s: %s"),
		    value, name, cf->name, _(error_type));
	case CONFIG_ORIGIN_STDIN:
		die(_("bad numeric config value '%s' for '%s' in standard input: %s"),
		    value, name, _(error_type));
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		die(_("bad numeric config value '%s' for '%s' in submodule-blob %s: %s"),
		    value, name, cf->name, _(error_type));
	case CONFIG_ORIGIN_CMDLINE:
		die(_("bad numeric config value '%s' for '%s' in command line %s: %s"),
		    value, name, cf->name, _(error_type));
	default:
		die(_("bad numeric config value '%s' for '%s' in %s: %s"),
		    value, name, cf->name, _(error_type));
	}
}

int git_config_int(const char *name, const char *value)
{
	int ret;
	if (!git_parse_int(value, &ret))
		die_bad_number(name, value);
	return ret;
}

int64_t git_config_int64(const char *name, const char *value)
{
	int64_t ret;
	if (!git_parse_int64(value, &ret))
		die_bad_number(name, value);
	return ret;
}

unsigned long git_config_ulong(const char *name, const char *value)
{
	unsigned long ret;
	if (!git_parse_ulong(value, &ret))
		die_bad_number(name, value);
	return ret;
}

ssize_t git_config_ssize_t(const char *name, const char *value)
{
	ssize_t ret;
	if (!git_parse_ssize_t(value, &ret))
		die_bad_number(name, value);
	return ret;
}

static int git_parse_maybe_bool_text(const char *value)
{
	if (!value)
		return 1;
	if (!*value)
		return 0;
	if (!strcasecmp(value, "true")
	    || !strcasecmp(value, "yes")
	    || !strcasecmp(value, "on"))
		return 1;
	if (!strcasecmp(value, "false")
	    || !strcasecmp(value, "no")
	    || !strcasecmp(value, "off"))
		return 0;
	return -1;
}

static const struct fsync_component_name {
	const char *name;
	enum fsync_component component_bits;
} fsync_component_names[] = {
	{ "loose-object", FSYNC_COMPONENT_LOOSE_OBJECT },
	{ "pack", FSYNC_COMPONENT_PACK },
	{ "pack-metadata", FSYNC_COMPONENT_PACK_METADATA },
	{ "commit-graph", FSYNC_COMPONENT_COMMIT_GRAPH },
	{ "index", FSYNC_COMPONENT_INDEX },
	{ "objects", FSYNC_COMPONENTS_OBJECTS },
	{ "reference", FSYNC_COMPONENT_REFERENCE },
	{ "derived-metadata", FSYNC_COMPONENTS_DERIVED_METADATA },
	{ "committed", FSYNC_COMPONENTS_COMMITTED },
	{ "added", FSYNC_COMPONENTS_ADDED },
	{ "all", FSYNC_COMPONENTS_ALL },
};

static enum fsync_component parse_fsync_components(const char *var, const char *string)
{
	enum fsync_component current = FSYNC_COMPONENTS_DEFAULT;
	enum fsync_component positive = 0, negative = 0;

	while (string) {
		int i;
		size_t len;
		const char *ep;
		int negated = 0;
		int found = 0;

		string = string + strspn(string, ", \t\n\r");
		ep = strchrnul(string, ',');
		len = ep - string;
		if (!strcmp(string, "none")) {
			current = FSYNC_COMPONENT_NONE;
			goto next_name;
		}

		if (*string == '-') {
			negated = 1;
			string++;
			len--;
			if (!len)
				warning(_("invalid value for variable %s"), var);
		}

		if (!len)
			break;

		for (i = 0; i < ARRAY_SIZE(fsync_component_names); ++i) {
			const struct fsync_component_name *n = &fsync_component_names[i];

			if (strncmp(n->name, string, len))
				continue;

			found = 1;
			if (negated)
				negative |= n->component_bits;
			else
				positive |= n->component_bits;
		}

		if (!found) {
			char *component = xstrndup(string, len);
			warning(_("ignoring unknown core.fsync component '%s'"), component);
			free(component);
		}

next_name:
		string = ep;
	}

	return (current & ~negative) | positive;
}

int git_parse_maybe_bool(const char *value)
{
	int v = git_parse_maybe_bool_text(value);
	if (0 <= v)
		return v;
	if (git_parse_int(value, &v))
		return !!v;
	return -1;
}

int git_config_bool_or_int(const char *name, const char *value, int *is_bool)
{
	int v = git_parse_maybe_bool_text(value);
	if (0 <= v) {
		*is_bool = 1;
		return v;
	}
	*is_bool = 0;
	return git_config_int(name, value);
}

int git_config_bool(const char *name, const char *value)
{
	int v = git_parse_maybe_bool(value);
	if (v < 0)
		die(_("bad boolean config value '%s' for '%s'"), value, name);
	return v;
}

int git_config_string(const char **dest, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	*dest = xstrdup(value);
	return 0;
}

int git_config_pathname(const char **dest, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	*dest = interpolate_path(value, 0);
	if (!*dest)
		die(_("failed to expand user dir in: '%s'"), value);
	return 0;
}

int git_config_expiry_date(timestamp_t *timestamp, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	if (parse_expiry_date(value, timestamp))
		return error(_("'%s' for '%s' is not a valid timestamp"),
			     value, var);
	return 0;
}

int git_config_color(char *dest, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	if (color_parse(value, dest) < 0)
		return -1;
	return 0;
}

static int git_default_core_config(const char *var, const char *value, void *cb)
{
	/* This needs a better name */
	if (!strcmp(var, "core.filemode")) {
		trust_executable_bit = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "core.trustctime")) {
		trust_ctime = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "core.checkstat")) {
		if (!strcasecmp(value, "default"))
			check_stat = 1;
		else if (!strcasecmp(value, "minimal"))
			check_stat = 0;
	}

	if (!strcmp(var, "core.quotepath")) {
		quote_path_fully = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.symlinks")) {
		has_symlinks = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.ignorecase")) {
		ignore_case = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.attributesfile"))
		return git_config_pathname(&git_attributes_file, var, value);

	if (!strcmp(var, "core.hookspath"))
		return git_config_pathname(&git_hooks_path, var, value);

	if (!strcmp(var, "core.bare")) {
		is_bare_repository_cfg = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.ignorestat")) {
		assume_unchanged = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.prefersymlinkrefs")) {
		prefer_symlink_refs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.logallrefupdates")) {
		if (value && !strcasecmp(value, "always"))
			log_all_ref_updates = LOG_REFS_ALWAYS;
		else if (git_config_bool(var, value))
			log_all_ref_updates = LOG_REFS_NORMAL;
		else
			log_all_ref_updates = LOG_REFS_NONE;
		return 0;
	}

	if (!strcmp(var, "core.warnambiguousrefs")) {
		warn_ambiguous_refs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.abbrev")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcasecmp(value, "auto"))
			default_abbrev = -1;
		else if (!git_parse_maybe_bool_text(value))
			default_abbrev = the_hash_algo->hexsz;
		else {
			int abbrev = git_config_int(var, value);
			if (abbrev < minimum_abbrev || abbrev > the_hash_algo->hexsz)
				return error(_("abbrev length out of range: %d"), abbrev);
			default_abbrev = abbrev;
		}
		return 0;
	}

	if (!strcmp(var, "core.disambiguate"))
		return set_disambiguate_hint_config(var, value);

	if (!strcmp(var, "core.loosecompression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad zlib compression level %d"), level);
		zlib_compression_level = level;
		zlib_compression_seen = 1;
		return 0;
	}

	if (!strcmp(var, "core.compression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad zlib compression level %d"), level);
		if (!zlib_compression_seen)
			zlib_compression_level = level;
		if (!pack_compression_seen)
			pack_compression_level = level;
		return 0;
	}

	if (!strcmp(var, "core.packedgitwindowsize")) {
		int pgsz_x2 = getpagesize() * 2;
		packed_git_window_size = git_config_ulong(var, value);

		/* This value must be multiple of (pagesize * 2) */
		packed_git_window_size /= pgsz_x2;
		if (packed_git_window_size < 1)
			packed_git_window_size = 1;
		packed_git_window_size *= pgsz_x2;
		return 0;
	}

	if (!strcmp(var, "core.bigfilethreshold")) {
		big_file_threshold = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "core.packedgitlimit")) {
		packed_git_limit = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "core.deltabasecachelimit")) {
		delta_base_cache_limit = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "core.autocrlf")) {
		if (value && !strcasecmp(value, "input")) {
			auto_crlf = AUTO_CRLF_INPUT;
			return 0;
		}
		auto_crlf = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.safecrlf")) {
		int eol_rndtrp_die;
		if (value && !strcasecmp(value, "warn")) {
			global_conv_flags_eol = CONV_EOL_RNDTRP_WARN;
			return 0;
		}
		eol_rndtrp_die = git_config_bool(var, value);
		global_conv_flags_eol = eol_rndtrp_die ?
			CONV_EOL_RNDTRP_DIE : 0;
		return 0;
	}

	if (!strcmp(var, "core.eol")) {
		if (value && !strcasecmp(value, "lf"))
			core_eol = EOL_LF;
		else if (value && !strcasecmp(value, "crlf"))
			core_eol = EOL_CRLF;
		else if (value && !strcasecmp(value, "native"))
			core_eol = EOL_NATIVE;
		else
			core_eol = EOL_UNSET;
		return 0;
	}

	if (!strcmp(var, "core.checkroundtripencoding")) {
		check_roundtrip_encoding = xstrdup(value);
		return 0;
	}

	if (!strcmp(var, "core.notesref")) {
		notes_ref_name = xstrdup(value);
		return 0;
	}

	if (!strcmp(var, "core.editor"))
		return git_config_string(&editor_program, var, value);

	if (!strcmp(var, "core.commentchar")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcasecmp(value, "auto"))
			auto_comment_line_char = 1;
		else if (value[0] && !value[1]) {
			comment_line_char = value[0];
			auto_comment_line_char = 0;
		} else
			return error(_("core.commentChar should only be one character"));
		return 0;
	}

	if (!strcmp(var, "core.askpass"))
		return git_config_string(&askpass_program, var, value);

	if (!strcmp(var, "core.excludesfile"))
		return git_config_pathname(&excludes_file, var, value);

	if (!strcmp(var, "core.whitespace")) {
		if (!value)
			return config_error_nonbool(var);
		whitespace_rule_cfg = parse_whitespace_rule(value);
		return 0;
	}

	if (!strcmp(var, "core.fsync")) {
		if (!value)
			return config_error_nonbool(var);
		fsync_components = parse_fsync_components(var, value);
		return 0;
	}

	if (!strcmp(var, "core.fsyncmethod")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcmp(value, "fsync"))
			fsync_method = FSYNC_METHOD_FSYNC;
		else if (!strcmp(value, "writeout-only"))
			fsync_method = FSYNC_METHOD_WRITEOUT_ONLY;
		else if (!strcmp(value, "batch"))
			fsync_method = FSYNC_METHOD_BATCH;
		else
			warning(_("ignoring unknown core.fsyncMethod value '%s'"), value);

	}

	if (!strcmp(var, "core.fsyncobjectfiles")) {
		if (fsync_object_files < 0)
			warning(_("core.fsyncObjectFiles is deprecated; use core.fsync instead"));
		fsync_object_files = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.preloadindex")) {
		core_preload_index = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.createobject")) {
		if (!strcmp(value, "rename"))
			object_creation_mode = OBJECT_CREATION_USES_RENAMES;
		else if (!strcmp(value, "link"))
			object_creation_mode = OBJECT_CREATION_USES_HARDLINKS;
		else
			die(_("invalid mode for object creation: %s"), value);
		return 0;
	}

	if (!strcmp(var, "core.sparsecheckout")) {
		core_apply_sparse_checkout = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.sparsecheckoutcone")) {
		core_sparse_checkout_cone = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.precomposeunicode")) {
		precomposed_unicode = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.protecthfs")) {
		protect_hfs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.protectntfs")) {
		protect_ntfs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.usereplacerefs")) {
		read_replace_refs = git_config_bool(var, value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return platform_core_config(var, value, cb);
}

static int git_default_sparse_config(const char *var, const char *value)
{
	if (!strcmp(var, "sparse.expectfilesoutsideofpatterns")) {
		sparse_expect_files_outside_of_patterns = git_config_bool(var, value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config/sparse.txt. */
	return 0;
}

static int git_default_i18n_config(const char *var, const char *value)
{
	if (!strcmp(var, "i18n.commitencoding"))
		return git_config_string(&git_commit_encoding, var, value);

	if (!strcmp(var, "i18n.logoutputencoding"))
		return git_config_string(&git_log_output_encoding, var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_branch_config(const char *var, const char *value)
{
	if (!strcmp(var, "branch.autosetupmerge")) {
		if (value && !strcmp(value, "always")) {
			git_branch_track = BRANCH_TRACK_ALWAYS;
			return 0;
		} else if (value && !strcmp(value, "inherit")) {
			git_branch_track = BRANCH_TRACK_INHERIT;
			return 0;
		}
		git_branch_track = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "branch.autosetuprebase")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcmp(value, "never"))
			autorebase = AUTOREBASE_NEVER;
		else if (!strcmp(value, "local"))
			autorebase = AUTOREBASE_LOCAL;
		else if (!strcmp(value, "remote"))
			autorebase = AUTOREBASE_REMOTE;
		else if (!strcmp(value, "always"))
			autorebase = AUTOREBASE_ALWAYS;
		else
			return error(_("malformed value for %s"), var);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_push_config(const char *var, const char *value)
{
	if (!strcmp(var, "push.default")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcmp(value, "nothing"))
			push_default = PUSH_DEFAULT_NOTHING;
		else if (!strcmp(value, "matching"))
			push_default = PUSH_DEFAULT_MATCHING;
		else if (!strcmp(value, "simple"))
			push_default = PUSH_DEFAULT_SIMPLE;
		else if (!strcmp(value, "upstream"))
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "tracking")) /* deprecated */
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "current"))
			push_default = PUSH_DEFAULT_CURRENT;
		else {
			error(_("malformed value for %s: %s"), var, value);
			return error(_("must be one of nothing, matching, simple, "
				       "upstream or current"));
		}
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_mailmap_config(const char *var, const char *value)
{
	if (!strcmp(var, "mailmap.file"))
		return git_config_pathname(&git_mailmap_file, var, value);
	if (!strcmp(var, "mailmap.blob"))
		return git_config_string(&git_mailmap_blob, var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int git_default_config(const char *var, const char *value, void *cb)
{
	if (starts_with(var, "core."))
		return git_default_core_config(var, value, cb);

	if (starts_with(var, "user.") ||
	    starts_with(var, "author.") ||
	    starts_with(var, "committer."))
		return git_ident_config(var, value, cb);

	if (starts_with(var, "i18n."))
		return git_default_i18n_config(var, value);

	if (starts_with(var, "branch."))
		return git_default_branch_config(var, value);

	if (starts_with(var, "push."))
		return git_default_push_config(var, value);

	if (starts_with(var, "mailmap."))
		return git_default_mailmap_config(var, value);

	if (starts_with(var, "advice.") || starts_with(var, "color.advice"))
		return git_default_advice_config(var, value);

	if (!strcmp(var, "pager.color") || !strcmp(var, "color.pager")) {
		pager_use_color = git_config_bool(var,value);
		return 0;
	}

	if (!strcmp(var, "pack.packsizelimit")) {
		pack_size_limit_cfg = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "pack.compression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad pack compression level %d"), level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}

	if (starts_with(var, "sparse."))
		return git_default_sparse_config(var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

/*
 * All source specific fields in the union, die_on_error, name and the callbacks
 * fgetc, ungetc, ftell of top need to be initialized before calling
 * this function.
 */
static int do_config_from(struct config_source *top, config_fn_t fn, void *data,
			  const struct config_options *opts)
{
	int ret;

	/* push config-file parsing state stack */
	top->prev = cf;
	top->linenr = 1;
	top->eof = 0;
	top->total_len = 0;
	strbuf_init(&top->value, 1024);
	strbuf_init(&top->var, 1024);
	cf = top;

	ret = git_parse_source(fn, data, opts);

	/* pop config-file parsing state stack */
	strbuf_release(&top->value);
	strbuf_release(&top->var);
	cf = top->prev;

	return ret;
}

static int do_config_from_file(config_fn_t fn,
		const enum config_origin_type origin_type,
		const char *name, const char *path, FILE *f,
		void *data, const struct config_options *opts)
{
	struct config_source top;
	int ret;

	top.u.file = f;
	top.origin_type = origin_type;
	top.name = name;
	top.path = path;
	top.default_error_action = CONFIG_ERROR_DIE;
	top.do_fgetc = config_file_fgetc;
	top.do_ungetc = config_file_ungetc;
	top.do_ftell = config_file_ftell;

	flockfile(f);
	ret = do_config_from(&top, fn, data, opts);
	funlockfile(f);
	return ret;
}

static int git_config_from_stdin(config_fn_t fn, void *data)
{
	return do_config_from_file(fn, CONFIG_ORIGIN_STDIN, "", NULL, stdin,
				   data, NULL);
}

int git_config_from_file_with_options(config_fn_t fn, const char *filename,
				      void *data,
				      const struct config_options *opts)
{
	int ret = -1;
	FILE *f;

	f = fopen_or_warn(filename, "r");
	if (f) {
		ret = do_config_from_file(fn, CONFIG_ORIGIN_FILE, filename,
					  filename, f, data, opts);
		fclose(f);
	}
	return ret;
}

int git_config_from_file(config_fn_t fn, const char *filename, void *data)
{
	return git_config_from_file_with_options(fn, filename, data, NULL);
}

int git_config_from_mem(config_fn_t fn,
			const enum config_origin_type origin_type,
			const char *name, const char *buf, size_t len,
			void *data, const struct config_options *opts)
{
	struct config_source top;

	top.u.buf.buf = buf;
	top.u.buf.len = len;
	top.u.buf.pos = 0;
	top.origin_type = origin_type;
	top.name = name;
	top.path = NULL;
	top.default_error_action = CONFIG_ERROR_ERROR;
	top.do_fgetc = config_buf_fgetc;
	top.do_ungetc = config_buf_ungetc;
	top.do_ftell = config_buf_ftell;

	return do_config_from(&top, fn, data, opts);
}

int git_config_from_blob_oid(config_fn_t fn,
			      const char *name,
			      struct repository *repo,
			      const struct object_id *oid,
			      void *data)
{
	enum object_type type;
	char *buf;
	unsigned long size;
	int ret;

	buf = repo_read_object_file(repo, oid, &type, &size);
	if (!buf)
		return error(_("unable to load config blob object '%s'"), name);
	if (type != OBJ_BLOB) {
		free(buf);
		return error(_("reference '%s' does not point to a blob"), name);
	}

	ret = git_config_from_mem(fn, CONFIG_ORIGIN_BLOB, name, buf, size,
				  data, NULL);
	free(buf);

	return ret;
}

static int git_config_from_blob_ref(config_fn_t fn,
				    struct repository *repo,
				    const char *name,
				    void *data)
{
	struct object_id oid;

	if (repo_get_oid(repo, name, &oid) < 0)
		return error(_("unable to resolve config blob '%s'"), name);
	return git_config_from_blob_oid(fn, name, repo, &oid, data);
}

char *git_system_config(void)
{
	char *system_config = xstrdup_or_null(getenv("GIT_CONFIG_SYSTEM"));
	if (!system_config)
		system_config = system_path(ETC_GITCONFIG);
	normalize_path_copy(system_config, system_config);
	return system_config;
}

void git_global_config(char **user_out, char **xdg_out)
{
	char *user_config = xstrdup_or_null(getenv("GIT_CONFIG_GLOBAL"));
	char *xdg_config = NULL;

	if (!user_config) {
		user_config = interpolate_path("~/.gitconfig", 0);
		xdg_config = xdg_config_home("config");
	}

	*user_out = user_config;
	*xdg_out = xdg_config;
}

/*
 * Parse environment variable 'k' as a boolean (in various
 * possible spellings); if missing, use the default value 'def'.
 */
int git_env_bool(const char *k, int def)
{
	const char *v = getenv(k);
	return v ? git_config_bool(k, v) : def;
}

/*
 * Parse environment variable 'k' as ulong with possibly a unit
 * suffix; if missing, use the default value 'val'.
 */
unsigned long git_env_ulong(const char *k, unsigned long val)
{
	const char *v = getenv(k);
	if (v && !git_parse_ulong(v, &val))
		die(_("failed to parse %s"), k);
	return val;
}

int git_config_system(void)
{
	return !git_env_bool("GIT_CONFIG_NOSYSTEM", 0);
}

static int do_git_config_sequence(const struct config_options *opts,
				  config_fn_t fn, void *data)
{
	int ret = 0;
	char *system_config = git_system_config();
	char *xdg_config = NULL;
	char *user_config = NULL;
	char *repo_config;
	enum config_scope prev_parsing_scope = current_parsing_scope;

	if (opts->commondir)
		repo_config = mkpathdup("%s/config", opts->commondir);
	else if (opts->git_dir)
		BUG("git_dir without commondir");
	else
		repo_config = NULL;

	current_parsing_scope = CONFIG_SCOPE_SYSTEM;
	if (git_config_system() && system_config &&
	    !access_or_die(system_config, R_OK,
			   opts->system_gently ? ACCESS_EACCES_OK : 0))
		ret += git_config_from_file(fn, system_config, data);

	current_parsing_scope = CONFIG_SCOPE_GLOBAL;
	git_global_config(&user_config, &xdg_config);

	if (xdg_config && !access_or_die(xdg_config, R_OK, ACCESS_EACCES_OK))
		ret += git_config_from_file(fn, xdg_config, data);

	if (user_config && !access_or_die(user_config, R_OK, ACCESS_EACCES_OK))
		ret += git_config_from_file(fn, user_config, data);

	current_parsing_scope = CONFIG_SCOPE_LOCAL;
	if (!opts->ignore_repo && repo_config &&
	    !access_or_die(repo_config, R_OK, 0))
		ret += git_config_from_file(fn, repo_config, data);

	current_parsing_scope = CONFIG_SCOPE_WORKTREE;
	if (!opts->ignore_worktree && repository_format_worktree_config) {
		char *path = git_pathdup("config.worktree");
		if (!access_or_die(path, R_OK, 0))
			ret += git_config_from_file(fn, path, data);
		free(path);
	}

	current_parsing_scope = CONFIG_SCOPE_COMMAND;
	if (!opts->ignore_cmdline && git_config_from_parameters(fn, data) < 0)
		die(_("unable to parse command-line config"));

	current_parsing_scope = prev_parsing_scope;
	free(system_config);
	free(xdg_config);
	free(user_config);
	free(repo_config);
	return ret;
}

int config_with_options(config_fn_t fn, void *data,
			struct git_config_source *config_source,
			const struct config_options *opts)
{
	struct config_include_data inc = CONFIG_INCLUDE_INIT;
	int ret;

	if (opts->respect_includes) {
		inc.fn = fn;
		inc.data = data;
		inc.opts = opts;
		inc.config_source = config_source;
		fn = git_config_include;
		data = &inc;
	}

	if (config_source)
		current_parsing_scope = config_source->scope;

	/*
	 * If we have a specific filename, use it. Otherwise, follow the
	 * regular lookup sequence.
	 */
	if (config_source && config_source->use_stdin) {
		ret = git_config_from_stdin(fn, data);
	} else if (config_source && config_source->file) {
		ret = git_config_from_file(fn, config_source->file, data);
	} else if (config_source && config_source->blob) {
		struct repository *repo = config_source->repo ?
			config_source->repo : the_repository;
		ret = git_config_from_blob_ref(fn, repo, config_source->blob,
						data);
	} else {
		ret = do_git_config_sequence(opts, fn, data);
	}

	if (inc.remote_urls) {
		string_list_clear(inc.remote_urls, 0);
		FREE_AND_NULL(inc.remote_urls);
	}
	return ret;
}

static void configset_iter(struct config_set *cs, config_fn_t fn, void *data)
{
	int i, value_index;
	struct string_list *values;
	struct config_set_element *entry;
	struct configset_list *list = &cs->list;

	for (i = 0; i < list->nr; i++) {
		entry = list->items[i].e;
		value_index = list->items[i].value_index;
		values = &entry->value_list;

		current_config_kvi = values->items[value_index].util;

		if (fn(entry->key, values->items[value_index].string, data) < 0)
			git_die_config_linenr(entry->key,
					      current_config_kvi->filename,
					      current_config_kvi->linenr);

		current_config_kvi = NULL;
	}
}

void read_early_config(config_fn_t cb, void *data)
{
	struct config_options opts = {0};
	struct strbuf commondir = STRBUF_INIT;
	struct strbuf gitdir = STRBUF_INIT;

	opts.respect_includes = 1;

	if (have_git_dir()) {
		opts.commondir = get_git_common_dir();
		opts.git_dir = get_git_dir();
	/*
	 * When setup_git_directory() was not yet asked to discover the
	 * GIT_DIR, we ask discover_git_directory() to figure out whether there
	 * is any repository config we should use (but unlike
	 * setup_git_directory_gently(), no global state is changed, most
	 * notably, the current working directory is still the same after the
	 * call).
	 */
	} else if (!discover_git_directory(&commondir, &gitdir)) {
		opts.commondir = commondir.buf;
		opts.git_dir = gitdir.buf;
	}

	config_with_options(cb, data, NULL, &opts);

	strbuf_release(&commondir);
	strbuf_release(&gitdir);
}

/*
 * Read config but only enumerate system and global settings.
 * Omit any repo-local, worktree-local, or command-line settings.
 */
void read_very_early_config(config_fn_t cb, void *data)
{
	struct config_options opts = { 0 };

	opts.respect_includes = 1;
	opts.ignore_repo = 1;
	opts.ignore_worktree = 1;
	opts.ignore_cmdline = 1;
	opts.system_gently = 1;

	config_with_options(cb, data, NULL, &opts);
}

static struct config_set_element *configset_find_element(struct config_set *cs, const char *key)
{
	struct config_set_element k;
	struct config_set_element *found_entry;
	char *normalized_key;
	/*
	 * `key` may come from the user, so normalize it before using it
	 * for querying entries from the hashmap.
	 */
	if (git_config_parse_key(key, &normalized_key, NULL))
		return NULL;

	hashmap_entry_init(&k.ent, strhash(normalized_key));
	k.key = normalized_key;
	found_entry = hashmap_get_entry(&cs->config_hash, &k, ent, NULL);
	free(normalized_key);
	return found_entry;
}

static int configset_add_value(struct config_set *cs, const char *key, const char *value)
{
	struct config_set_element *e;
	struct string_list_item *si;
	struct configset_list_item *l_item;
	struct key_value_info *kv_info = xmalloc(sizeof(*kv_info));

	e = configset_find_element(cs, key);
	/*
	 * Since the keys are being fed by git_config*() callback mechanism, they
	 * are already normalized. So simply add them without any further munging.
	 */
	if (!e) {
		e = xmalloc(sizeof(*e));
		hashmap_entry_init(&e->ent, strhash(key));
		e->key = xstrdup(key);
		string_list_init_dup(&e->value_list);
		hashmap_add(&cs->config_hash, &e->ent);
	}
	si = string_list_append_nodup(&e->value_list, xstrdup_or_null(value));

	ALLOC_GROW(cs->list.items, cs->list.nr + 1, cs->list.alloc);
	l_item = &cs->list.items[cs->list.nr++];
	l_item->e = e;
	l_item->value_index = e->value_list.nr - 1;

	if (!cf)
		BUG("configset_add_value has no source");
	if (cf->name) {
		kv_info->filename = strintern(cf->name);
		kv_info->linenr = cf->linenr;
		kv_info->origin_type = cf->origin_type;
	} else {
		/* for values read from `git_config_from_parameters()` */
		kv_info->filename = NULL;
		kv_info->linenr = -1;
		kv_info->origin_type = CONFIG_ORIGIN_CMDLINE;
	}
	kv_info->scope = current_parsing_scope;
	si->util = kv_info;

	return 0;
}

static int config_set_element_cmp(const void *unused_cmp_data,
				  const struct hashmap_entry *eptr,
				  const struct hashmap_entry *entry_or_key,
				  const void *unused_keydata)
{
	const struct config_set_element *e1, *e2;

	e1 = container_of(eptr, const struct config_set_element, ent);
	e2 = container_of(entry_or_key, const struct config_set_element, ent);

	return strcmp(e1->key, e2->key);
}

void git_configset_init(struct config_set *cs)
{
	hashmap_init(&cs->config_hash, config_set_element_cmp, NULL, 0);
	cs->hash_initialized = 1;
	cs->list.nr = 0;
	cs->list.alloc = 0;
	cs->list.items = NULL;
}

void git_configset_clear(struct config_set *cs)
{
	struct config_set_element *entry;
	struct hashmap_iter iter;
	if (!cs->hash_initialized)
		return;

	hashmap_for_each_entry(&cs->config_hash, &iter, entry,
				ent /* member name */) {
		free(entry->key);
		string_list_clear(&entry->value_list, 1);
	}
	hashmap_clear_and_free(&cs->config_hash, struct config_set_element, ent);
	cs->hash_initialized = 0;
	free(cs->list.items);
	cs->list.nr = 0;
	cs->list.alloc = 0;
	cs->list.items = NULL;
}

static int config_set_callback(const char *key, const char *value, void *cb)
{
	struct config_set *cs = cb;
	configset_add_value(cs, key, value);
	return 0;
}

int git_configset_add_file(struct config_set *cs, const char *filename)
{
	return git_config_from_file(config_set_callback, filename, cs);
}

int git_configset_get_value(struct config_set *cs, const char *key, const char **value)
{
	const struct string_list *values = NULL;
	/*
	 * Follows "last one wins" semantic, i.e., if there are multiple matches for the
	 * queried key in the files of the configset, the value returned will be the last
	 * value in the value list for that key.
	 */
	values = git_configset_get_value_multi(cs, key);

	if (!values)
		return 1;
	assert(values->nr > 0);
	*value = values->items[values->nr - 1].string;
	return 0;
}

const struct string_list *git_configset_get_value_multi(struct config_set *cs, const char *key)
{
	struct config_set_element *e = configset_find_element(cs, key);
	return e ? &e->value_list : NULL;
}

int git_configset_get_string(struct config_set *cs, const char *key, char **dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value))
		return git_config_string((const char **)dest, key, value);
	else
		return 1;
}

static int git_configset_get_string_tmp(struct config_set *cs, const char *key,
					const char **dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		if (!value)
			return config_error_nonbool(key);
		*dest = value;
		return 0;
	} else {
		return 1;
	}
}

int git_configset_get_int(struct config_set *cs, const char *key, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_int(key, value);
		return 0;
	} else
		return 1;
}

int git_configset_get_ulong(struct config_set *cs, const char *key, unsigned long *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_ulong(key, value);
		return 0;
	} else
		return 1;
}

int git_configset_get_bool(struct config_set *cs, const char *key, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_bool(key, value);
		return 0;
	} else
		return 1;
}

int git_configset_get_bool_or_int(struct config_set *cs, const char *key,
				int *is_bool, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_bool_or_int(key, value, is_bool);
		return 0;
	} else
		return 1;
}

int git_configset_get_maybe_bool(struct config_set *cs, const char *key, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_parse_maybe_bool(value);
		if (*dest == -1)
			return -1;
		return 0;
	} else
		return 1;
}

int git_configset_get_pathname(struct config_set *cs, const char *key, const char **dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value))
		return git_config_pathname(dest, key, value);
	else
		return 1;
}

/* Functions use to read configuration from a repository */
static void repo_read_config(struct repository *repo)
{
	struct config_options opts = { 0 };

	opts.respect_includes = 1;
	opts.commondir = repo->commondir;
	opts.git_dir = repo->gitdir;

	if (!repo->config)
		CALLOC_ARRAY(repo->config, 1);
	else
		git_configset_clear(repo->config);

	git_configset_init(repo->config);

	if (config_with_options(config_set_callback, repo->config, NULL, &opts) < 0)
		/*
		 * config_with_options() normally returns only
		 * zero, as most errors are fatal, and
		 * non-fatal potential errors are guarded by "if"
		 * statements that are entered only when no error is
		 * possible.
		 *
		 * If we ever encounter a non-fatal error, it means
		 * something went really wrong and we should stop
		 * immediately.
		 */
		die(_("unknown error occurred while reading the configuration files"));
}

static void git_config_check_init(struct repository *repo)
{
	if (repo->config && repo->config->hash_initialized)
		return;
	repo_read_config(repo);
}

static void repo_config_clear(struct repository *repo)
{
	if (!repo->config || !repo->config->hash_initialized)
		return;
	git_configset_clear(repo->config);
}

void repo_config(struct repository *repo, config_fn_t fn, void *data)
{
	git_config_check_init(repo);
	configset_iter(repo->config, fn, data);
}

int repo_config_get_value(struct repository *repo,
			  const char *key, const char **value)
{
	git_config_check_init(repo);
	return git_configset_get_value(repo->config, key, value);
}

const struct string_list *repo_config_get_value_multi(struct repository *repo,
						      const char *key)
{
	git_config_check_init(repo);
	return git_configset_get_value_multi(repo->config, key);
}

int repo_config_get_string(struct repository *repo,
			   const char *key, char **dest)
{
	int ret;
	git_config_check_init(repo);
	ret = git_configset_get_string(repo->config, key, dest);
	if (ret < 0)
		git_die_config(key, NULL);
	return ret;
}

int repo_config_get_string_tmp(struct repository *repo,
			       const char *key, const char **dest)
{
	int ret;
	git_config_check_init(repo);
	ret = git_configset_get_string_tmp(repo->config, key, dest);
	if (ret < 0)
		git_die_config(key, NULL);
	return ret;
}

int repo_config_get_int(struct repository *repo,
			const char *key, int *dest)
{
	git_config_check_init(repo);
	return git_configset_get_int(repo->config, key, dest);
}

int repo_config_get_ulong(struct repository *repo,
			  const char *key, unsigned long *dest)
{
	git_config_check_init(repo);
	return git_configset_get_ulong(repo->config, key, dest);
}

int repo_config_get_bool(struct repository *repo,
			 const char *key, int *dest)
{
	git_config_check_init(repo);
	return git_configset_get_bool(repo->config, key, dest);
}

int repo_config_get_bool_or_int(struct repository *repo,
				const char *key, int *is_bool, int *dest)
{
	git_config_check_init(repo);
	return git_configset_get_bool_or_int(repo->config, key, is_bool, dest);
}

int repo_config_get_maybe_bool(struct repository *repo,
			       const char *key, int *dest)
{
	git_config_check_init(repo);
	return git_configset_get_maybe_bool(repo->config, key, dest);
}

int repo_config_get_pathname(struct repository *repo,
			     const char *key, const char **dest)
{
	int ret;
	git_config_check_init(repo);
	ret = git_configset_get_pathname(repo->config, key, dest);
	if (ret < 0)
		git_die_config(key, NULL);
	return ret;
}

/* Functions used historically to read configuration from 'the_repository' */
void git_config(config_fn_t fn, void *data)
{
	repo_config(the_repository, fn, data);
}

void git_config_clear(void)
{
	repo_config_clear(the_repository);
}

int git_config_get_value(const char *key, const char **value)
{
	return repo_config_get_value(the_repository, key, value);
}

const struct string_list *git_config_get_value_multi(const char *key)
{
	return repo_config_get_value_multi(the_repository, key);
}

int git_config_get_string(const char *key, char **dest)
{
	return repo_config_get_string(the_repository, key, dest);
}

int git_config_get_string_tmp(const char *key, const char **dest)
{
	return repo_config_get_string_tmp(the_repository, key, dest);
}

int git_config_get_int(const char *key, int *dest)
{
	return repo_config_get_int(the_repository, key, dest);
}

int git_config_get_ulong(const char *key, unsigned long *dest)
{
	return repo_config_get_ulong(the_repository, key, dest);
}

int git_config_get_bool(const char *key, int *dest)
{
	return repo_config_get_bool(the_repository, key, dest);
}

int git_config_get_bool_or_int(const char *key, int *is_bool, int *dest)
{
	return repo_config_get_bool_or_int(the_repository, key, is_bool, dest);
}

int git_config_get_maybe_bool(const char *key, int *dest)
{
	return repo_config_get_maybe_bool(the_repository, key, dest);
}

int git_config_get_pathname(const char *key, const char **dest)
{
	return repo_config_get_pathname(the_repository, key, dest);
}

int git_config_get_expiry(const char *key, const char **output)
{
	int ret = git_config_get_string(key, (char **)output);
	if (ret)
		return ret;
	if (strcmp(*output, "now")) {
		timestamp_t now = approxidate("now");
		if (approxidate(*output) >= now)
			git_die_config(key, _("Invalid %s: '%s'"), key, *output);
	}
	return ret;
}

int git_config_get_expiry_in_days(const char *key, timestamp_t *expiry, timestamp_t now)
{
	const char *expiry_string;
	intmax_t days;
	timestamp_t when;

	if (git_config_get_string_tmp(key, &expiry_string))
		return 1; /* no such thing */

	if (git_parse_signed(expiry_string, &days, maximum_signed_value_of_type(int))) {
		const int scale = 86400;
		*expiry = now - days * scale;
		return 0;
	}

	if (!parse_expiry_date(expiry_string, &when)) {
		*expiry = when;
		return 0;
	}
	return -1; /* thing exists but cannot be parsed */
}

int git_config_get_split_index(void)
{
	int val;

	if (!git_config_get_maybe_bool("core.splitindex", &val))
		return val;

	return -1; /* default value */
}

int git_config_get_max_percent_split_change(void)
{
	int val = -1;

	if (!git_config_get_int("splitindex.maxpercentchange", &val)) {
		if (0 <= val && val <= 100)
			return val;

		return error(_("splitIndex.maxPercentChange value '%d' "
			       "should be between 0 and 100"), val);
	}

	return -1; /* default value */
}

int git_config_get_index_threads(int *dest)
{
	int is_bool, val;

	val = git_env_ulong("GIT_TEST_INDEX_THREADS", 0);
	if (val) {
		*dest = val;
		return 0;
	}

	if (!git_config_get_bool_or_int("index.threads", &is_bool, &val)) {
		if (is_bool)
			*dest = val ? 0 : 1;
		else
			*dest = val;
		return 0;
	}

	return 1;
}

NORETURN
void git_die_config_linenr(const char *key, const char *filename, int linenr)
{
	if (!filename)
		die(_("unable to parse '%s' from command-line config"), key);
	else
		die(_("bad config variable '%s' in file '%s' at line %d"),
		    key, filename, linenr);
}

NORETURN __attribute__((format(printf, 2, 3)))
void git_die_config(const char *key, const char *err, ...)
{
	const struct string_list *values;
	struct key_value_info *kv_info;
	report_fn error_fn = get_error_routine();

	if (err) {
		va_list params;
		va_start(params, err);
		error_fn(err, params);
		va_end(params);
	}
	values = git_config_get_value_multi(key);
	kv_info = values->items[values->nr - 1].util;
	git_die_config_linenr(key, kv_info->filename, kv_info->linenr);
}

/*
 * Find all the stuff for git_config_set() below.
 */

struct config_store_data {
	size_t baselen;
	char *key;
	int do_not_match;
	const char *fixed_value;
	regex_t *value_pattern;
	int multi_replace;
	struct {
		size_t begin, end;
		enum config_event_t type;
		int is_keys_section;
	} *parsed;
	unsigned int parsed_nr, parsed_alloc, *seen, seen_nr, seen_alloc;
	unsigned int key_seen:1, section_seen:1, is_keys_section:1;
};

static void config_store_data_clear(struct config_store_data *store)
{
	free(store->key);
	if (store->value_pattern != NULL &&
	    store->value_pattern != CONFIG_REGEX_NONE) {
		regfree(store->value_pattern);
		free(store->value_pattern);
	}
	free(store->parsed);
	free(store->seen);
	memset(store, 0, sizeof(*store));
}

static int matches(const char *key, const char *value,
		   const struct config_store_data *store)
{
	if (strcmp(key, store->key))
		return 0; /* not ours */
	if (store->fixed_value)
		return !strcmp(store->fixed_value, value);
	if (!store->value_pattern)
		return 1; /* always matches */
	if (store->value_pattern == CONFIG_REGEX_NONE)
		return 0; /* never matches */

	return store->do_not_match ^
		(value && !regexec(store->value_pattern, value, 0, NULL, 0));
}

static int store_aux_event(enum config_event_t type,
			   size_t begin, size_t end, void *data)
{
	struct config_store_data *store = data;

	ALLOC_GROW(store->parsed, store->parsed_nr + 1, store->parsed_alloc);
	store->parsed[store->parsed_nr].begin = begin;
	store->parsed[store->parsed_nr].end = end;
	store->parsed[store->parsed_nr].type = type;

	if (type == CONFIG_EVENT_SECTION) {
		int (*cmpfn)(const char *, const char *, size_t);

		if (cf->var.len < 2 || cf->var.buf[cf->var.len - 1] != '.')
			return error(_("invalid section name '%s'"), cf->var.buf);

		if (cf->subsection_case_sensitive)
			cmpfn = strncasecmp;
		else
			cmpfn = strncmp;

		/* Is this the section we were looking for? */
		store->is_keys_section =
			store->parsed[store->parsed_nr].is_keys_section =
			cf->var.len - 1 == store->baselen &&
			!cmpfn(cf->var.buf, store->key, store->baselen);
		if (store->is_keys_section) {
			store->section_seen = 1;
			ALLOC_GROW(store->seen, store->seen_nr + 1,
				   store->seen_alloc);
			store->seen[store->seen_nr] = store->parsed_nr;
		}
	}

	store->parsed_nr++;

	return 0;
}

static int store_aux(const char *key, const char *value, void *cb)
{
	struct config_store_data *store = cb;

	if (store->key_seen) {
		if (matches(key, value, store)) {
			if (store->seen_nr == 1 && store->multi_replace == 0) {
				warning(_("%s has multiple values"), key);
			}

			ALLOC_GROW(store->seen, store->seen_nr + 1,
				   store->seen_alloc);

			store->seen[store->seen_nr] = store->parsed_nr;
			store->seen_nr++;
		}
	} else if (store->is_keys_section) {
		/*
		 * Do not increment matches yet: this may not be a match, but we
		 * are in the desired section.
		 */
		ALLOC_GROW(store->seen, store->seen_nr + 1, store->seen_alloc);
		store->seen[store->seen_nr] = store->parsed_nr;
		store->section_seen = 1;

		if (matches(key, value, store)) {
			store->seen_nr++;
			store->key_seen = 1;
		}
	}

	return 0;
}

static int write_error(const char *filename)
{
	error(_("failed to write new configuration file %s"), filename);

	/* Same error code as "failed to rename". */
	return 4;
}

static struct strbuf store_create_section(const char *key,
					  const struct config_store_data *store)
{
	const char *dot;
	size_t i;
	struct strbuf sb = STRBUF_INIT;

	dot = memchr(key, '.', store->baselen);
	if (dot) {
		strbuf_addf(&sb, "[%.*s \"", (int)(dot - key), key);
		for (i = dot - key + 1; i < store->baselen; i++) {
			if (key[i] == '"' || key[i] == '\\')
				strbuf_addch(&sb, '\\');
			strbuf_addch(&sb, key[i]);
		}
		strbuf_addstr(&sb, "\"]\n");
	} else {
		strbuf_addch(&sb, '[');
		strbuf_add(&sb, key, store->baselen);
		strbuf_addstr(&sb, "]\n");
	}

	return sb;
}

static ssize_t write_section(int fd, const char *key,
			     const struct config_store_data *store)
{
	struct strbuf sb = store_create_section(key, store);
	ssize_t ret;

	ret = write_in_full(fd, sb.buf, sb.len);
	strbuf_release(&sb);

	return ret;
}

static ssize_t write_pair(int fd, const char *key, const char *value,
			  const struct config_store_data *store)
{
	int i;
	ssize_t ret;
	const char *quote = "";
	struct strbuf sb = STRBUF_INIT;

	/*
	 * Check to see if the value needs to be surrounded with a dq pair.
	 * Note that problematic characters are always backslash-quoted; this
	 * check is about not losing leading or trailing SP and strings that
	 * follow beginning-of-comment characters (i.e. ';' and '#') by the
	 * configuration parser.
	 */
	if (value[0] == ' ')
		quote = "\"";
	for (i = 0; value[i]; i++)
		if (value[i] == ';' || value[i] == '#')
			quote = "\"";
	if (i && value[i - 1] == ' ')
		quote = "\"";

	strbuf_addf(&sb, "\t%s = %s", key + store->baselen + 1, quote);

	for (i = 0; value[i]; i++)
		switch (value[i]) {
		case '\n':
			strbuf_addstr(&sb, "\\n");
			break;
		case '\t':
			strbuf_addstr(&sb, "\\t");
			break;
		case '"':
		case '\\':
			strbuf_addch(&sb, '\\');
			/* fallthrough */
		default:
			strbuf_addch(&sb, value[i]);
			break;
		}
	strbuf_addf(&sb, "%s\n", quote);

	ret = write_in_full(fd, sb.buf, sb.len);
	strbuf_release(&sb);

	return ret;
}

/*
 * If we are about to unset the last key(s) in a section, and if there are
 * no comments surrounding (or included in) the section, we will want to
 * extend begin/end to remove the entire section.
 *
 * Note: the parameter `seen_ptr` points to the index into the store.seen
 * array.  * This index may be incremented if a section has more than one
 * entry (which all are to be removed).
 */
static void maybe_remove_section(struct config_store_data *store,
				 size_t *begin_offset, size_t *end_offset,
				 int *seen_ptr)
{
	size_t begin;
	int i, seen, section_seen = 0;

	/*
	 * First, ensure that this is the first key, and that there are no
	 * comments before the entry nor before the section header.
	 */
	seen = *seen_ptr;
	for (i = store->seen[seen]; i > 0; i--) {
		enum config_event_t type = store->parsed[i - 1].type;

		if (type == CONFIG_EVENT_COMMENT)
			/* There is a comment before this entry or section */
			return;
		if (type == CONFIG_EVENT_ENTRY) {
			if (!section_seen)
				/* This is not the section's first entry. */
				return;
			/* We encountered no comment before the section. */
			break;
		}
		if (type == CONFIG_EVENT_SECTION) {
			if (!store->parsed[i - 1].is_keys_section)
				break;
			section_seen = 1;
		}
	}
	begin = store->parsed[i].begin;

	/*
	 * Next, make sure that we are removing the last key(s) in the section,
	 * and that there are no comments that are possibly about the current
	 * section.
	 */
	for (i = store->seen[seen] + 1; i < store->parsed_nr; i++) {
		enum config_event_t type = store->parsed[i].type;

		if (type == CONFIG_EVENT_COMMENT)
			return;
		if (type == CONFIG_EVENT_SECTION) {
			if (store->parsed[i].is_keys_section)
				continue;
			break;
		}
		if (type == CONFIG_EVENT_ENTRY) {
			if (++seen < store->seen_nr &&
			    i == store->seen[seen])
				/* We want to remove this entry, too */
				continue;
			/* There is another entry in this section. */
			return;
		}
	}

	/*
	 * We are really removing the last entry/entries from this section, and
	 * there are no enclosed or surrounding comments. Remove the entire,
	 * now-empty section.
	 */
	*seen_ptr = seen;
	*begin_offset = begin;
	if (i < store->parsed_nr)
		*end_offset = store->parsed[i].begin;
	else
		*end_offset = store->parsed[store->parsed_nr - 1].end;
}

int git_config_set_in_file_gently(const char *config_filename,
				  const char *key, const char *value)
{
	return git_config_set_multivar_in_file_gently(config_filename, key, value, NULL, 0);
}

void git_config_set_in_file(const char *config_filename,
			    const char *key, const char *value)
{
	git_config_set_multivar_in_file(config_filename, key, value, NULL, 0);
}

int git_config_set_gently(const char *key, const char *value)
{
	return git_config_set_multivar_gently(key, value, NULL, 0);
}

int repo_config_set_worktree_gently(struct repository *r,
				    const char *key, const char *value)
{
	/* Only use worktree-specific config if it is is already enabled. */
	if (repository_format_worktree_config) {
		char *file = repo_git_path(r, "config.worktree");
		int ret = git_config_set_multivar_in_file_gently(
					file, key, value, NULL, 0);
		free(file);
		return ret;
	}
	return repo_config_set_multivar_gently(r, key, value, NULL, 0);
}

void git_config_set(const char *key, const char *value)
{
	git_config_set_multivar(key, value, NULL, 0);

	trace2_cmd_set_config(key, value);
}

/*
 * If value==NULL, unset in (remove from) config,
 * if value_pattern!=NULL, disregard key/value pairs where value does not match.
 * if value_pattern==CONFIG_REGEX_NONE, do not match any existing values
 *     (only add a new one)
 * if flags contains the CONFIG_FLAGS_MULTI_REPLACE flag, all matching
 *     key/values are removed before a single new pair is written. If the
 *     flag is not present, then replace only the first match.
 *
 * Returns 0 on success.
 *
 * This function does this:
 *
 * - it locks the config file by creating ".git/config.lock"
 *
 * - it then parses the config using store_aux() as validator to find
 *   the position on the key/value pair to replace. If it is to be unset,
 *   it must be found exactly once.
 *
 * - the config file is mmap()ed and the part before the match (if any) is
 *   written to the lock file, then the changed part and the rest.
 *
 * - the config file is removed and the lock file rename()d to it.
 *
 */
int git_config_set_multivar_in_file_gently(const char *config_filename,
					   const char *key, const char *value,
					   const char *value_pattern,
					   unsigned flags)
{
	int fd = -1, in_fd = -1;
	int ret;
	struct lock_file lock = LOCK_INIT;
	char *filename_buf = NULL;
	char *contents = NULL;
	size_t contents_sz;
	struct config_store_data store;

	memset(&store, 0, sizeof(store));

	/* parse-key returns negative; flip the sign to feed exit(3) */
	ret = 0 - git_config_parse_key(key, &store.key, &store.baselen);
	if (ret)
		goto out_free;

	store.multi_replace = (flags & CONFIG_FLAGS_MULTI_REPLACE) != 0;

	if (!config_filename)
		config_filename = filename_buf = git_pathdup("config");

	/*
	 * The lock serves a purpose in addition to locking: the new
	 * contents of .git/config will be written into it.
	 */
	fd = hold_lock_file_for_update(&lock, config_filename, 0);
	if (fd < 0) {
		error_errno(_("could not lock config file %s"), config_filename);
		ret = CONFIG_NO_LOCK;
		goto out_free;
	}

	/*
	 * If .git/config does not exist yet, write a minimal version.
	 */
	in_fd = open(config_filename, O_RDONLY);
	if ( in_fd < 0 ) {
		if ( ENOENT != errno ) {
			error_errno(_("opening %s"), config_filename);
			ret = CONFIG_INVALID_FILE; /* same as "invalid config file" */
			goto out_free;
		}
		/* if nothing to unset, error out */
		if (value == NULL) {
			ret = CONFIG_NOTHING_SET;
			goto out_free;
		}

		free(store.key);
		store.key = xstrdup(key);
		if (write_section(fd, key, &store) < 0 ||
		    write_pair(fd, key, value, &store) < 0)
			goto write_err_out;
	} else {
		struct stat st;
		size_t copy_begin, copy_end;
		int i, new_line = 0;
		struct config_options opts;

		if (value_pattern == NULL)
			store.value_pattern = NULL;
		else if (value_pattern == CONFIG_REGEX_NONE)
			store.value_pattern = CONFIG_REGEX_NONE;
		else if (flags & CONFIG_FLAGS_FIXED_VALUE)
			store.fixed_value = value_pattern;
		else {
			if (value_pattern[0] == '!') {
				store.do_not_match = 1;
				value_pattern++;
			} else
				store.do_not_match = 0;

			store.value_pattern = (regex_t*)xmalloc(sizeof(regex_t));
			if (regcomp(store.value_pattern, value_pattern,
					REG_EXTENDED)) {
				error(_("invalid pattern: %s"), value_pattern);
				FREE_AND_NULL(store.value_pattern);
				ret = CONFIG_INVALID_PATTERN;
				goto out_free;
			}
		}

		ALLOC_GROW(store.parsed, 1, store.parsed_alloc);
		store.parsed[0].end = 0;

		memset(&opts, 0, sizeof(opts));
		opts.event_fn = store_aux_event;
		opts.event_fn_data = &store;

		/*
		 * After this, store.parsed will contain offsets of all the
		 * parsed elements, and store.seen will contain a list of
		 * matches, as indices into store.parsed.
		 *
		 * As a side effect, we make sure to transform only a valid
		 * existing config file.
		 */
		if (git_config_from_file_with_options(store_aux,
						      config_filename,
						      &store, &opts)) {
			error(_("invalid config file %s"), config_filename);
			ret = CONFIG_INVALID_FILE;
			goto out_free;
		}

		/* if nothing to unset, or too many matches, error out */
		if ((store.seen_nr == 0 && value == NULL) ||
		    (store.seen_nr > 1 && !store.multi_replace)) {
			ret = CONFIG_NOTHING_SET;
			goto out_free;
		}

		if (fstat(in_fd, &st) == -1) {
			error_errno(_("fstat on %s failed"), config_filename);
			ret = CONFIG_INVALID_FILE;
			goto out_free;
		}

		contents_sz = xsize_t(st.st_size);
		contents = xmmap_gently(NULL, contents_sz, PROT_READ,
					MAP_PRIVATE, in_fd, 0);
		if (contents == MAP_FAILED) {
			if (errno == ENODEV && S_ISDIR(st.st_mode))
				errno = EISDIR;
			error_errno(_("unable to mmap '%s'%s"),
					config_filename, mmap_os_err());
			ret = CONFIG_INVALID_FILE;
			contents = NULL;
			goto out_free;
		}
		close(in_fd);
		in_fd = -1;

		if (chmod(get_lock_file_path(&lock), st.st_mode & 07777) < 0) {
			error_errno(_("chmod on %s failed"), get_lock_file_path(&lock));
			ret = CONFIG_NO_WRITE;
			goto out_free;
		}

		if (store.seen_nr == 0) {
			if (!store.seen_alloc) {
				/* Did not see key nor section */
				ALLOC_GROW(store.seen, 1, store.seen_alloc);
				store.seen[0] = store.parsed_nr
					- !!store.parsed_nr;
			}
			store.seen_nr = 1;
		}

		for (i = 0, copy_begin = 0; i < store.seen_nr; i++) {
			size_t replace_end;
			int j = store.seen[i];

			new_line = 0;
			if (!store.key_seen) {
				copy_end = store.parsed[j].end;
				/* include '\n' when copying section header */
				if (copy_end > 0 && copy_end < contents_sz &&
				    contents[copy_end - 1] != '\n' &&
				    contents[copy_end] == '\n')
					copy_end++;
				replace_end = copy_end;
			} else {
				replace_end = store.parsed[j].end;
				copy_end = store.parsed[j].begin;
				if (!value)
					maybe_remove_section(&store,
							     &copy_end,
							     &replace_end, &i);
				/*
				 * Swallow preceding white-space on the same
				 * line.
				 */
				while (copy_end > 0 ) {
					char c = contents[copy_end - 1];

					if (isspace(c) && c != '\n')
						copy_end--;
					else
						break;
				}
			}

			if (copy_end > 0 && contents[copy_end-1] != '\n')
				new_line = 1;

			/* write the first part of the config */
			if (copy_end > copy_begin) {
				if (write_in_full(fd, contents + copy_begin,
						  copy_end - copy_begin) < 0)
					goto write_err_out;
				if (new_line &&
				    write_str_in_full(fd, "\n") < 0)
					goto write_err_out;
			}
			copy_begin = replace_end;
		}

		/* write the pair (value == NULL means unset) */
		if (value != NULL) {
			if (!store.section_seen) {
				if (write_section(fd, key, &store) < 0)
					goto write_err_out;
			}
			if (write_pair(fd, key, value, &store) < 0)
				goto write_err_out;
		}

		/* write the rest of the config */
		if (copy_begin < contents_sz)
			if (write_in_full(fd, contents + copy_begin,
					  contents_sz - copy_begin) < 0)
				goto write_err_out;

		munmap(contents, contents_sz);
		contents = NULL;
	}

	if (commit_lock_file(&lock) < 0) {
		error_errno(_("could not write config file %s"), config_filename);
		ret = CONFIG_NO_WRITE;
		goto out_free;
	}

	ret = 0;

	/* Invalidate the config cache */
	git_config_clear();

out_free:
	rollback_lock_file(&lock);
	free(filename_buf);
	if (contents)
		munmap(contents, contents_sz);
	if (in_fd >= 0)
		close(in_fd);
	config_store_data_clear(&store);
	return ret;

write_err_out:
	ret = write_error(get_lock_file_path(&lock));
	goto out_free;

}

void git_config_set_multivar_in_file(const char *config_filename,
				     const char *key, const char *value,
				     const char *value_pattern, unsigned flags)
{
	if (!git_config_set_multivar_in_file_gently(config_filename, key, value,
						    value_pattern, flags))
		return;
	if (value)
		die(_("could not set '%s' to '%s'"), key, value);
	else
		die(_("could not unset '%s'"), key);
}

int git_config_set_multivar_gently(const char *key, const char *value,
				   const char *value_pattern, unsigned flags)
{
	return repo_config_set_multivar_gently(the_repository, key, value,
					       value_pattern, flags);
}

int repo_config_set_multivar_gently(struct repository *r, const char *key,
				    const char *value,
				    const char *value_pattern, unsigned flags)
{
	char *file = repo_git_path(r, "config");
	int res = git_config_set_multivar_in_file_gently(file,
							 key, value,
							 value_pattern,
							 flags);
	free(file);
	return res;
}

void git_config_set_multivar(const char *key, const char *value,
			     const char *value_pattern, unsigned flags)
{
	git_config_set_multivar_in_file(git_path("config"),
					key, value, value_pattern,
					flags);
}

static int section_name_match (const char *buf, const char *name)
{
	int i = 0, j = 0, dot = 0;
	if (buf[i] != '[')
		return 0;
	for (i = 1; buf[i] && buf[i] != ']'; i++) {
		if (!dot && isspace(buf[i])) {
			dot = 1;
			if (name[j++] != '.')
				break;
			for (i++; isspace(buf[i]); i++)
				; /* do nothing */
			if (buf[i] != '"')
				break;
			continue;
		}
		if (buf[i] == '\\' && dot)
			i++;
		else if (buf[i] == '"' && dot) {
			for (i++; isspace(buf[i]); i++)
				; /* do_nothing */
			break;
		}
		if (buf[i] != name[j++])
			break;
	}
	if (buf[i] == ']' && name[j] == 0) {
		/*
		 * We match, now just find the right length offset by
		 * gobbling up any whitespace after it, as well
		 */
		i++;
		for (; buf[i] && isspace(buf[i]); i++)
			; /* do nothing */
		return i;
	}
	return 0;
}

static int section_name_is_ok(const char *name)
{
	/* Empty section names are bogus. */
	if (!*name)
		return 0;

	/*
	 * Before a dot, we must be alphanumeric or dash. After the first dot,
	 * anything goes, so we can stop checking.
	 */
	for (; *name && *name != '.'; name++)
		if (*name != '-' && !isalnum(*name))
			return 0;
	return 1;
}

/* if new_name == NULL, the section is removed instead */
static int git_config_copy_or_rename_section_in_file(const char *config_filename,
				      const char *old_name,
				      const char *new_name, int copy)
{
	int ret = 0, remove = 0;
	char *filename_buf = NULL;
	struct lock_file lock = LOCK_INIT;
	int out_fd;
	char buf[1024];
	FILE *config_file = NULL;
	struct stat st;
	struct strbuf copystr = STRBUF_INIT;
	struct config_store_data store;

	memset(&store, 0, sizeof(store));

	if (new_name && !section_name_is_ok(new_name)) {
		ret = error(_("invalid section name: %s"), new_name);
		goto out_no_rollback;
	}

	if (!config_filename)
		config_filename = filename_buf = git_pathdup("config");

	out_fd = hold_lock_file_for_update(&lock, config_filename, 0);
	if (out_fd < 0) {
		ret = error(_("could not lock config file %s"), config_filename);
		goto out;
	}

	if (!(config_file = fopen(config_filename, "rb"))) {
		ret = warn_on_fopen_errors(config_filename);
		if (ret)
			goto out;
		/* no config file means nothing to rename, no error */
		goto commit_and_out;
	}

	if (fstat(fileno(config_file), &st) == -1) {
		ret = error_errno(_("fstat on %s failed"), config_filename);
		goto out;
	}

	if (chmod(get_lock_file_path(&lock), st.st_mode & 07777) < 0) {
		ret = error_errno(_("chmod on %s failed"),
				  get_lock_file_path(&lock));
		goto out;
	}

	while (fgets(buf, sizeof(buf), config_file)) {
		unsigned i;
		int length;
		int is_section = 0;
		char *output = buf;
		for (i = 0; buf[i] && isspace(buf[i]); i++)
			; /* do nothing */
		if (buf[i] == '[') {
			/* it's a section */
			int offset;
			is_section = 1;

			/*
			 * When encountering a new section under -c we
			 * need to flush out any section we're already
			 * coping and begin anew. There might be
			 * multiple [branch "$name"] sections.
			 */
			if (copystr.len > 0) {
				if (write_in_full(out_fd, copystr.buf, copystr.len) < 0) {
					ret = write_error(get_lock_file_path(&lock));
					goto out;
				}
				strbuf_reset(&copystr);
			}

			offset = section_name_match(&buf[i], old_name);
			if (offset > 0) {
				ret++;
				if (new_name == NULL) {
					remove = 1;
					continue;
				}
				store.baselen = strlen(new_name);
				if (!copy) {
					if (write_section(out_fd, new_name, &store) < 0) {
						ret = write_error(get_lock_file_path(&lock));
						goto out;
					}
					/*
					 * We wrote out the new section, with
					 * a newline, now skip the old
					 * section's length
					 */
					output += offset + i;
					if (strlen(output) > 0) {
						/*
						 * More content means there's
						 * a declaration to put on the
						 * next line; indent with a
						 * tab
						 */
						output -= 1;
						output[0] = '\t';
					}
				} else {
					copystr = store_create_section(new_name, &store);
				}
			}
			remove = 0;
		}
		if (remove)
			continue;
		length = strlen(output);

		if (!is_section && copystr.len > 0) {
			strbuf_add(&copystr, output, length);
		}

		if (write_in_full(out_fd, output, length) < 0) {
			ret = write_error(get_lock_file_path(&lock));
			goto out;
		}
	}

	/*
	 * Copy a trailing section at the end of the config, won't be
	 * flushed by the usual "flush because we have a new section
	 * logic in the loop above.
	 */
	if (copystr.len > 0) {
		if (write_in_full(out_fd, copystr.buf, copystr.len) < 0) {
			ret = write_error(get_lock_file_path(&lock));
			goto out;
		}
		strbuf_reset(&copystr);
	}

	fclose(config_file);
	config_file = NULL;
commit_and_out:
	if (commit_lock_file(&lock) < 0)
		ret = error_errno(_("could not write config file %s"),
				  config_filename);
out:
	if (config_file)
		fclose(config_file);
	rollback_lock_file(&lock);
out_no_rollback:
	free(filename_buf);
	config_store_data_clear(&store);
	return ret;
}

int git_config_rename_section_in_file(const char *config_filename,
				      const char *old_name, const char *new_name)
{
	return git_config_copy_or_rename_section_in_file(config_filename,
					 old_name, new_name, 0);
}

int git_config_rename_section(const char *old_name, const char *new_name)
{
	return git_config_rename_section_in_file(NULL, old_name, new_name);
}

int git_config_copy_section_in_file(const char *config_filename,
				      const char *old_name, const char *new_name)
{
	return git_config_copy_or_rename_section_in_file(config_filename,
					 old_name, new_name, 1);
}

int git_config_copy_section(const char *old_name, const char *new_name)
{
	return git_config_copy_section_in_file(NULL, old_name, new_name);
}

/*
 * Call this to report error for your variable that should not
 * get a boolean value (i.e. "[my] var" means "true").
 */
#undef config_error_nonbool
int config_error_nonbool(const char *var)
{
	return error(_("missing value for '%s'"), var);
}

int parse_config_key(const char *var,
		     const char *section,
		     const char **subsection, size_t *subsection_len,
		     const char **key)
{
	const char *dot;

	/* Does it start with "section." ? */
	if (!skip_prefix(var, section, &var) || *var != '.')
		return -1;

	/*
	 * Find the key; we don't know yet if we have a subsection, but we must
	 * parse backwards from the end, since the subsection may have dots in
	 * it, too.
	 */
	dot = strrchr(var, '.');
	*key = dot + 1;

	/* Did we have a subsection at all? */
	if (dot == var) {
		if (subsection) {
			*subsection = NULL;
			*subsection_len = 0;
		}
	}
	else {
		if (!subsection)
			return -1;
		*subsection = var + 1;
		*subsection_len = dot - *subsection;
	}

	return 0;
}

const char *current_config_origin_type(void)
{
	int type;
	if (current_config_kvi)
		type = current_config_kvi->origin_type;
	else if(cf)
		type = cf->origin_type;
	else
		BUG("current_config_origin_type called outside config callback");

	switch (type) {
	case CONFIG_ORIGIN_BLOB:
		return "blob";
	case CONFIG_ORIGIN_FILE:
		return "file";
	case CONFIG_ORIGIN_STDIN:
		return "standard input";
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		return "submodule-blob";
	case CONFIG_ORIGIN_CMDLINE:
		return "command line";
	default:
		BUG("unknown config origin type");
	}
}

const char *config_scope_name(enum config_scope scope)
{
	switch (scope) {
	case CONFIG_SCOPE_SYSTEM:
		return "system";
	case CONFIG_SCOPE_GLOBAL:
		return "global";
	case CONFIG_SCOPE_LOCAL:
		return "local";
	case CONFIG_SCOPE_WORKTREE:
		return "worktree";
	case CONFIG_SCOPE_COMMAND:
		return "command";
	case CONFIG_SCOPE_SUBMODULE:
		return "submodule";
	default:
		return "unknown";
	}
}

const char *current_config_name(void)
{
	const char *name;
	if (current_config_kvi)
		name = current_config_kvi->filename;
	else if (cf)
		name = cf->name;
	else
		BUG("current_config_name called outside config callback");
	return name ? name : "";
}

enum config_scope current_config_scope(void)
{
	if (current_config_kvi)
		return current_config_kvi->scope;
	else
		return current_parsing_scope;
}

int current_config_line(void)
{
	if (current_config_kvi)
		return current_config_kvi->linenr;
	else
		return cf->linenr;
}

int lookup_config(const char **mapping, int nr_mapping, const char *var)
{
	int i;

	for (i = 0; i < nr_mapping; i++) {
		const char *name = mapping[i];

		if (name && !strcasecmp(var, name))
			return i;
	}
	return -1;
}
